#!/usr/bin/env python3
"""Shared Mac/Windows/Linux sync wrapper. Secrets stay out of process arguments.

Ships with the Inkpot plugin (Plugins/Inkpot/Scripts) and may also be copied to <Project>/Scripts.
The project it serves is PROSETTA_PROJECT_ROOT (the editor exports it), else the nearest folder above
this script that holds a .uproject.
"""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import queue
import re
import subprocess
import ssl
import shutil
import signal
import sys
import tempfile
import threading
import urllib.error
import urllib.parse
import urllib.request

def project_root(environment=None, script=None):
    """The Unreal project this wrapper serves: PROSETTA_PROJECT_ROOT, else the closest .uproject folder above the script."""
    environment = os.environ if environment is None else environment
    if environment.get('PROSETTA_PROJECT_ROOT'):
        return Path(environment['PROSETTA_PROJECT_ROOT']).resolve()
    here = Path(script or __file__).resolve().parent
    for folder in [here, *here.parents]:
        if any(folder.glob('*.uproject')):
            return folder
    return here.parent


EDITOR_ONLY_MODULE_TYPES = ('Editor', 'EditorNoCommandlet', 'EditorAndProgram', 'Developer', 'DeveloperTool', 'UncookedOnly', 'Program')


def native_bindings_destination(root, environment=None):
    """Where `native-bindings` copies Prosetta's generated C++: PROSETTA_NATIVE_BINDINGS_DIR, else
    Source/<first runtime .uproject module>/Prosetta/NativeBindings. None when the project has no runtime C++ module."""
    environment = os.environ if environment is None else environment
    if environment.get('PROSETTA_NATIVE_BINDINGS_DIR'):
        path = Path(environment['PROSETTA_NATIVE_BINDINGS_DIR'])
        return path if path.is_absolute() else root / path
    for project in sorted(Path(root).glob('*.uproject')):
        try:
            modules = json.loads(project.read_text(encoding='utf-8-sig')).get('Modules') or []
        except (OSError, ValueError):
            continue
        # The bindings are game code: an editor-only module would leave packaged builds without them.
        name = next((module.get('Name') for module in modules if isinstance(module, dict) and module.get('Name')
                     and module.get('Type', 'Runtime') not in EDITOR_ONLY_MODULE_TYPES), None)
        if name:
            return Path(root) / 'Source' / name / 'Prosetta' / 'NativeBindings'
    return None


def helper_script(name):
    """A companion script next to this wrapper, else in the project's Scripts folder; None when neither exists."""
    for candidate in (Path(__file__).resolve().with_name(name), ROOT / 'Scripts' / name):
        if candidate.is_file():
            return candidate
    return None


ROOT = project_root()
LOCAL = ROOT / '.prosetta-sync' / 'config.json'
LEGACY = ROOT / 'Saved' / 'ProsettaSync' / 'prosetta-sync.config.json'
HOME = Path.home() / '.prosetta-sync' / 'config.json'
DEFAULT_BASE_URL = 'https://api.prosetta.studio'
WATCH_SCHEMA_VERSION = 1  # `prosetta-sync watch` NDJSON events ( CLI 0.10.0+ )
NATIVE_SOURCE = ROOT / 'Saved' / 'Prosetta' / 'native-bindings' / 'cpp'
NATIVE_DEST = native_bindings_destination(ROOT)
NATIVE_MANIFEST = ROOT / 'Saved' / 'Prosetta' / 'native-bindings' / 'manifest.json'
PROSETTA_HEADER = '// inkpot-native-binding module='
LOOPBACK_HOSTS = ('localhost', '127.0.0.1', '::1')


class ExpiredToken(RuntimeError):
    pass


class Terminated(BaseException):
    """SIGTERM, e.g. the Unreal editor stopping `watch`; unwinds so the running CLI is stopped too."""


def raise_terminated(signum, frame):
    raise Terminated()


def read_config(path):
    if not path.is_file():
        return {}
    with path.open(encoding='utf-8-sig') as stream:
        return json.load(stream)


def loopback_host(base):
    """Host of a base URL that points at this machine (a local Prosetta dev server), else None."""
    host = (urllib.parse.urlsplit(base).hostname or '').lower()
    return host if host in LOOPBACK_HOSTS or host.endswith('.localhost') else None


def require_secure_base(base):
    # Plain HTTP is accepted only for a dev server on this machine; tokens never cross the network unencrypted.
    if base.startswith('https://') or (base.startswith('http://') and loopback_host(base)):
        return
    raise RuntimeError('Prosetta credentials require an HTTPS baseUrl (plain http only for localhost).')


def introspect(settings, token):
    base = settings.get('baseUrl') or DEFAULT_BASE_URL
    require_secure_base(base)
    request = urllib.request.Request(base.rstrip('/') + '/api/engine-sync/v1/project-token-introspection',
                                     headers={'Authorization': f'Bearer {token}'})
    try:
        context = ssl.create_default_context()
        engine = os.environ.get('UE_ROOT')
        if not engine:
            engine_dir = next((p for p in Path(sys.executable).resolve().parents if p.name == 'Engine'), None)
            if engine_dir: engine = str(engine_dir.parent)
        if engine:
            certificates = Path(engine) / 'Engine/Content/Certificates/ThirdParty/cacert.pem'
            if certificates.is_file():
                context.load_verify_locations(cafile=str(certificates))
        with urllib.request.urlopen(request, timeout=30, context=context) as response:
            identity = json.load(response)
    except urllib.error.HTTPError as error:
        try:
            payload = json.loads(error.read())
        except (ValueError, OSError):
            payload = {}
        if error.code in (401, 403) and payload.get('code') == 'INVALID_OR_EXPIRED_TOKEN':
            raise ExpiredToken('Prosetta rejected the token as INVALID_OR_EXPIRED_TOKEN.') from None
        raise RuntimeError(f'Prosetta credential check failed with HTTP {error.code}; no fallback attempted.') from None
    expected_id = settings.get('projectId')
    expected_identifier = settings.get('projectIdentifier')
    if expected_id and str(identity.get('projectId')) != str(expected_id):
        raise RuntimeError('Credential belongs to a different project; refusing to replace this project mirror.')
    if not expected_id and expected_identifier and identity.get('projectIdentifier') != expected_identifier:
        raise RuntimeError('Credential belongs to a different project identifier; refusing to sync.')
    return identity


def native_config_paths():
    """Where the Prosetta Sync CLI keeps its own user config (the only layer the CLI defines).

    Mirrors Prosetta.SyncTool ConfigService.ResolveConfigPath, which is
    Environment.SpecialFolder.ApplicationData / Prosetta / SyncTool / config.json:
    ~/Library/Application Support on macOS (confirmed against CLI 0.11.0, which ignores a $HOME override
    there), %APPDATA% (Roaming) on Windows, $XDG_CONFIG_HOME or ~/.config on Linux. The CLI never reads
    or writes %LOCALAPPDATA% for config, so it is not a candidate. The CLI has no offline command that
    reports this, so the wrapper has to know the path; keep it in step with ConfigService.
    """
    if sys.platform == 'darwin':
        return [Path.home() / 'Library/Application Support/Prosetta/SyncTool/config.json']
    if sys.platform == 'win32':
        return [Path(os.environ.get('APPDATA') or Path.home() / 'AppData' / 'Roaming') / 'Prosetta/SyncTool/config.json']
    # An empty XDG_CONFIG_HOME means unset, as it does for .NET.
    return [Path(os.environ.get('XDG_CONFIG_HOME') or Path.home() / '.config') / 'Prosetta/SyncTool/config.json']


def fallback_config():
    for path in [HOME, *native_config_paths()]:
        settings = read_config(path)
        if settings.get('token'):
            return settings
    return {}


def select_credentials(local, home_loader=lambda: fallback_config(), environment=None, check=introspect):
    environment = os.environ if environment is None else environment
    settings = dict(local)
    if environment.get('PROSETTA_BASE_URL'):
        settings['baseUrl'] = environment['PROSETTA_BASE_URL']
    if environment.get('PROSETTA_PROJECT_ID'):
        settings['projectId'] = int(environment['PROSETTA_PROJECT_ID'])
    explicit = environment.get('PROSETTA_PROJECT_TOKEN')
    if explicit:
        check(settings, explicit)  # Explicit environment override never falls back.
        return settings, explicit
    token = local.get('token')
    if token:
        try:
            check(settings, token)
            return settings, token
        except ExpiredToken:
            print('Local Prosetta token expired; checking home credentials.', file=sys.stderr)
    home = home_loader()
    token = home.get('token')
    if not token:
        raise RuntimeError('No usable token. Set PROSETTA_PROJECT_TOKEN or sign in with prosetta-sync, or add token to ~/.prosetta-sync/config.json.')
    # A home config is a complete fallback only if no local config exists.
    # When local settings exist, home contributes only the token.
    if not local:
        settings = dict(home)
        for env_key, config_key in [('PROSETTA_BASE_URL', 'baseUrl'), ('PROSETTA_PROJECT_ID', 'projectId')]:
            if environment.get(env_key):
                settings[config_key] = int(environment[env_key]) if config_key == 'projectId' else environment[env_key]
    elif not settings.get('projectId') and not settings.get('projectIdentifier'):
        raise RuntimeError('Local config has no project identity; set project-id before using home credentials.')
    check(settings, token)
    print('Using home Prosetta credentials for this project.', file=sys.stderr)
    return settings, token


def cache_path(settings):
    key = str(settings.get('projectId') or settings.get('projectIdentifier') or 'default')
    local_host = loopback_host(settings.get('baseUrl') or '')
    if local_host:
        # Dev-server project ids restart at 1 and would otherwise share a cache with production projects.
        port = urllib.parse.urlsplit(settings['baseUrl']).port
        key = f'{local_host}-{port or 80}-{key}'
    scope = settings.get('volumes')
    if isinstance(scope, list):
        scope = ','.join(scope)
    if scope:
        key += '-' + hashlib.sha256(scope.encode()).hexdigest()[:12]
    key = re.sub(r'[^A-Za-z0-9._-]+', '_', key)
    if sys.platform == 'darwin':
        base = Path.home() / 'Library' / 'Application Support'
    elif sys.platform == 'win32':
        base = Path(os.environ.get('LOCALAPPDATA', Path.home() / 'AppData' / 'Local'))
    else:
        base = Path(os.environ.get('XDG_CACHE_HOME', Path.home() / '.cache'))
    return base / 'Prosetta' / 'SyncTool' / 'cache' / key


def tool_command():
    override = os.environ.get('PROSETTA_SYNC_BIN')
    if override:
        return [override]
    runtime = shutil.which('dotnet')
    if not runtime:
        # GUI editors do not inherit a login shell's PATH.
        candidates = ([Path('/opt/homebrew/bin/dotnet'), Path('/usr/local/share/dotnet/dotnet'),
                       Path('/usr/local/bin/dotnet')] if sys.platform == 'darwin' else
                      [Path(os.environ.get('ProgramFiles', 'C:/Program Files')) / 'dotnet/dotnet.exe']
                      if sys.platform == 'win32' else [Path('/usr/share/dotnet/dotnet')])
        runtime = next((str(path) for path in candidates if path.is_file()), 'dotnet')
    vendored = ROOT / 'Tools/ProsettaSync/publish/Prosetta.SyncTool.dll'
    if not vendored.is_file():
        # No CLI build vendored into the project: use an installed prosetta-sync when there is one.
        installed = shutil.which('prosetta-sync') or next((str(path) for path in (
            Path.home() / '.local/bin/prosetta-sync', Path('/usr/local/bin/prosetta-sync'),
            Path('/opt/homebrew/bin/prosetta-sync')) if path.is_file()), None)
        if installed:
            return [installed]
    return [runtime, str(vendored)]


def option_value(args, name):
    index = args.index(name)
    if index + 1 >= len(args) or args[index + 1].startswith('--'):
        raise RuntimeError(f'{name} requires a value.')
    return args[index + 1]


def take_option(args, name):
    """Removes `name value` or `name=value` from args and returns the value, or None when absent."""
    if name in args:
        value = option_value(args, name)
        index = args.index(name)
        del args[index:index + 2]
        return value
    prefixed = next((x for x in args if x.startswith(name + '=')), None)
    if prefixed is None:
        return None
    args.remove(prefixed)
    return prefixed.split('=', 1)[1]


def file_mode(path):
    """Permission bits of a file as a three-digit octal string, or None where they do not apply."""
    if os.name != 'posix':
        return None
    try:
        return f'{os.stat(path).st_mode & 0o777:03o}'
    except OSError:
        return None


def describe_credentials(environment=None):
    """Offline report of where credentials would come from, for the editor dashboard.

    Never includes a token: the report names files, whether each holds a token, and its mode.
    No network access and no CLI invocation, so it is safe to call before either is available.
    """
    environment = os.environ if environment is None else environment

    def describe(path, kind):
        entry = {'kind': kind, 'path': str(path), 'exists': path.is_file(), 'hasToken': False, 'mode': None, 'warning': None}
        if not entry['exists']:
            return entry
        try:
            entry['hasToken'] = bool(read_config(path).get('token'))
        except (OSError, ValueError):
            entry['warning'] = 'Config file could not be read as JSON.'
        entry['mode'] = file_mode(path)
        if entry['hasToken'] and entry['mode'] and int(entry['mode'], 8) & 0o077:
            entry['warning'] = f'Token file is readable by other users (mode {entry["mode"]}); run chmod 600 on it.'
        return entry

    candidates = [describe(LOCAL, 'local'), describe(LEGACY, 'local'), describe(HOME, 'home')]
    candidates += [describe(path, 'native') for path in native_config_paths()]
    local_path = LOCAL if LOCAL.is_file() or not LEGACY.is_file() else LEGACY
    try:
        local = read_config(local_path)
    except (OSError, ValueError):
        local = {}
    if environment.get('PROSETTA_PROJECT_TOKEN'):
        source = 'env'
    else:
        source = next((entry['kind'] for entry in candidates if entry['hasToken']), 'none')
    try:
        settings = dict(local) if local else {k: v for k, v in fallback_config().items() if k != 'token'}
    except (OSError, ValueError):
        settings = dict(local)
    settings.pop('token', None)
    overridden = False
    if environment.get('PROSETTA_BASE_URL'):
        settings['baseUrl'] = environment['PROSETTA_BASE_URL']; overridden = True
    if environment.get('PROSETTA_PROJECT_ID'):
        settings['projectId'] = environment['PROSETTA_PROJECT_ID']; overridden = True
    settings['baseUrl'] = settings.get('baseUrl') or DEFAULT_BASE_URL
    command = tool_command()
    runtime = command[0]
    cli_available = (Path(runtime).is_file() or bool(shutil.which(runtime))) and all(Path(part).is_file() for part in command[1:])
    return {
        'schemaVersion': 1,
        'source': source,
        'localConfigPath': str(local_path),
        'localConfigExists': local_path.is_file(),
        'baseUrl': settings['baseUrl'],
        'projectId': settings.get('projectId'),
        'projectIdentifier': settings.get('projectIdentifier'),
        'projectName': settings.get('projectName'),
        'volumes': settings.get('volumes'),
        'sessionOverride': overridden,
        'cachePath': str(cache_path(settings)),
        'mirrorPath': str(ROOT / 'Saved' / 'Prosetta'),
        'cli': {'command': runtime, 'available': cli_available},
        'candidates': candidates,
    }


def stop_cli(process, grace=10):
    """Stops a CLI that no signal of its own will reach, killing it if it ignores the request."""
    process.terminate()
    try:
        process.wait(timeout=grace)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


def stream_cli(command, env, on_lines):
    """Runs the CLI, echoing stdout and handing each batch of lines to on_lines on this thread.

    A reader thread drains the pipe so the CLI never blocks on a full pipe while
    on_lines runs a long import; lines that arrive meanwhile form the next batch.
    on_lines may return a non-zero code to stop the CLI.
    """
    with subprocess.Popen(command, env=env, stdout=subprocess.PIPE, text=True, encoding='utf-8', bufsize=1) as process:
        lines = queue.Queue()
        def read():
            for line in process.stdout:
                lines.put(line)
            lines.put(None)
        threading.Thread(target=read, daemon=True).start()
        try:
            while (line := lines.get()) is not None:
                backlog = [line]
                while not lines.empty() and backlog[-1] is not None:
                    backlog.append(lines.get())
                finished = backlog[-1] is None
                backlog = [entry for entry in backlog if entry is not None]
                for entry in backlog:
                    sys.stdout.write(entry)
                sys.stdout.flush()
                code = on_lines(backlog)
                if code:
                    stop_cli(process)
                    return code
                if finished:
                    break
            return process.wait()
        except KeyboardInterrupt:
            # The CLI receives the same Ctrl+C, cancels pending work and keeps completed generations.
            return process.wait()
        except BaseException:
            # An idle watch never exits on its own, so Popen's exit wait would hang on a failed consumer.
            stop_cli(process)
            raise


def run_foreground(command, env):
    """Runs the CLI attached to this terminal, letting Ctrl+C stop it without a traceback."""
    with subprocess.Popen(command, env=env) as process:
        try:
            return process.wait()
        except KeyboardInterrupt:
            # The CLI receives the same Ctrl+C and exits on its own.
            return process.wait()
        except BaseException:
            # Nothing else reaches the CLI (SIGTERM goes to this wrapper only), so Popen's exit wait would hang.
            stop_cli(process)
            raise


def run_cli(args, on_lines=None):
    local_path = LOCAL if LOCAL.is_file() or not LEGACY.is_file() else LEGACY
    local = read_config(local_path)
    credential_env = dict(os.environ)
    args = list(args)
    if '--token' in args:
        credential_env['PROSETTA_PROJECT_TOKEN'] = option_value(args, '--token')
        index = args.index('--token')
        del args[index:index + 2]
    elif any(x.startswith('--token=') for x in args):
        arg = next(x for x in args if x.startswith('--token='))
        credential_env['PROSETTA_PROJECT_TOKEN'] = arg.split('=', 1)[1]
        args.remove(arg)
    if '--token-stdin' in args:
        # The token arrives as one line on standard input: never in argv, the process list or a log.
        args.remove('--token-stdin')
        line = sys.stdin.readline().strip()
        if not line:
            raise RuntimeError('--token-stdin expects the token as one line on standard input.')
        credential_env['PROSETTA_PROJECT_TOKEN'] = line
    base_url = option_value(args, '--base-url') if '--base-url' in args else None
    if args[0] == 'init':
        # init keeps --project-id for the CLI; the token check must target that project, not the old local one.
        project = option_value(args, '--project-id') if '--project-id' in args else next(
            (x.split('=', 1)[1] for x in args if x.startswith('--project-id=')), None)
        if project is not None:
            if not project.isdigit():
                raise RuntimeError('--project-id requires a numeric project id.')
            credential_env['PROSETTA_PROJECT_ID'] = project
    else:
        # Session targets from the dashboard or a terminal: stripped before the CLI sees them and applied
        # like PROSETTA_BASE_URL / PROSETTA_PROJECT_ID, so they are never written to the local config.
        for name, key in (('--base-url', 'PROSETTA_BASE_URL'), ('--project-id', 'PROSETTA_PROJECT_ID')):
            value = take_option(args, name)
            if value is None:
                continue
            if key == 'PROSETTA_PROJECT_ID' and not value.isdigit():
                raise RuntimeError('--project-id requires a numeric project id.')
            credential_env[key] = value
        base_url = None
    remote = args[0] in ('sync', 'watch', 'whoami', 'volumes', 'init')
    # A one-off target (another server or project) must not be written over the reviewed local config.
    overridden = args[0] not in ('init', 'config') and bool(
        base_url or credential_env.get('PROSETTA_BASE_URL') or credential_env.get('PROSETTA_PROJECT_ID'))
    if remote:
        if base_url:
            # Applied like PROSETTA_BASE_URL so a home-only setup still counts as "no local config".
            credential_env['PROSETTA_BASE_URL'] = base_url
        settings, token = select_credentials(local, environment=credential_env)
        settings.pop('token', None)  # The CLI receives the token via PROSETTA_PROJECT_TOKEN.
    else:
        # Fallback credentials are never persisted locally, so drop the token from a fallback seed.
        settings = dict(local) if local else {k: v for k, v in fallback_config().items() if k != 'token'}
        token = None
        if credential_env.get('PROSETTA_BASE_URL'):
            settings['baseUrl'] = credential_env['PROSETTA_BASE_URL']
        if credential_env.get('PROSETTA_PROJECT_ID'):
            settings['projectId'] = int(credential_env['PROSETTA_PROJECT_ID'])
        if base_url:
            settings['baseUrl'] = base_url
    settings['baseUrl'] = settings.get('baseUrl') or DEFAULT_BASE_URL
    settings.update(cachePath=str(cache_path(settings)), unrealProjectPath=str(ROOT), mirrorPath=str(ROOT / 'Saved' / 'Prosetta'))
    if args[0] == 'init':
        # CLI init requires an explicit base URL; keep the reviewed project paths.
        if '--base-url' not in args: args.extend(['--base-url', settings['baseUrl']])
        for option, key in [('cache', 'cachePath'), ('unreal-project', 'unrealProjectPath'), ('mirror', 'mirrorPath')]:
            if '--' + option not in args: args.extend(['--' + option, settings[key]])
    # Never let the CLI rewrite the shared home config or persist fallback credentials.
    saved = ROOT / 'Saved' / 'ProsettaSync'
    saved.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='run-', dir=saved) as temporary:
        config = Path(temporary) / 'config.json'
        # Created owner-only: a local token can still be present for config commands.
        with os.fdopen(os.open(config, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), 'w', encoding='utf-8') as output:
            json.dump(settings, output)
        env = dict(os.environ)
        if token:
            env['PROSETTA_PROJECT_TOKEN'] = token
        command = [*tool_command(), *args, '--config', str(config)]
        returncode = stream_cli(command, env, on_lines) if on_lines else run_foreground(command, env)
        if returncode == 0 and overridden:
            print('Session override (PROSETTA_BASE_URL/PROSETTA_PROJECT_ID/--base-url) in use; local config left unchanged.', file=sys.stderr)
        elif returncode == 0:
            updated = read_config(config)
            if args[0] != 'config':
                updated['token'] = credential_env.get('PROSETTA_PROJECT_TOKEN') if args[0] == 'init' and credential_env.get('PROSETTA_PROJECT_TOKEN') else local.get('token')  # Never persist an env/home credential.
            local_path.parent.mkdir(parents=True, exist_ok=True)
            with local_path.open('w', encoding='utf-8') as output:
                json.dump(updated, output, indent=2)
                output.write('\n')
            local_path.chmod(0o600)
        return returncode


def display_path(path):
    """Project-relative path when possible; destinations outside the project print as given."""
    path = Path(path)
    try: return path.resolve().relative_to(ROOT.resolve()).as_posix()
    except ValueError: return str(path)


def copy_native_bindings(source=NATIVE_SOURCE, destination=NATIVE_DEST, manifest=NATIVE_MANIFEST, out=print):
    """Copy Prosetta-generated native binding sources from the mirror into the game module.

    Only .cpp files are copied and only when their content changed, so an unchanged sync
    leaves the build untouched. Files in the destination that carry the Prosetta header but no
    longer exist in the mirror are reported, never deleted: removing bindings is a human decision.
    Returns (copied, stale) file name lists.
    """
    source = Path(source)
    mirrored = sorted(path for path in source.glob('*.cpp')) if source.is_dir() else []
    if not mirrored:
        out('No Prosetta native binding sources in the mirror; nothing to copy.')
        return [], []
    if destination is None:
        out('Prosetta native binding sources were synced, but this project has no runtime C++ module to copy them into; '
            'set PROSETTA_NATIVE_BINDINGS_DIR to a module folder to copy them.')
        return [], []
    destination = Path(destination)
    destination.mkdir(parents=True, exist_ok=True)
    copied = []
    for path in mirrored:
        target = destination / path.name
        content = path.read_bytes()
        if not target.exists() or target.read_bytes() != content:
            target.write_bytes(content)
            copied.append(path.name)
    names = {path.name for path in mirrored}
    stale = []
    for path in sorted(destination.glob('*.cpp')):
        if path.name in names: continue
        try: head = path.read_text(encoding='utf-8', errors='replace').splitlines()[:4]
        except OSError: continue
        if any(line.startswith(PROSETTA_HEADER) and line.rstrip().endswith('source=prosetta') for line in head):
            stale.append(path.name)
    out(f'Copied {len(copied)} native binding file(s) into {display_path(destination)}' + (': ' + ', '.join(copied) if copied else '.'))
    for name in stale:
        out(f'Stale Prosetta binding no longer in the mirror (delete by hand if intended): {name}')
    if copied:
        out('Rebuild the project\'s Editor target, or regenerate loaders in the editor with:')
        out(f'  Prosetta.NativeBindings.Generate "{destination}" "{Path(manifest)}"')
    return copied, stale


def run_helper(name, module_name, args):
    """Runs an optional project helper (import-prosetta.py, prosetta-relay.py) found by helper_script."""
    path = helper_script(name)
    if path is None:
        raise RuntimeError(f'This command needs Scripts/{name} in the project (a project-specific helper the Inkpot plugin does not ship).')
    spec = importlib.util.spec_from_file_location(module_name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.run(args)


def run_import(args):
    return run_helper('import-prosetta.py', 'prosetta_import', args)


def run_relay(args):
    return run_helper('prosetta-relay.py', 'prosetta_relay', args)


def watch_arguments(args):
    """Arguments for native `prosetta-sync watch`; drops the pre-0.10 polling interval."""
    args = list(args)
    if args and args[0].isdigit():
        print('Watch no longer polls; ignoring the interval. prosetta-sync watch streams generations as they complete.', file=sys.stderr)
        args.pop(0)
    return ['watch', *args]


def generation_import_args(event):
    """Importer arguments for a generation-completed event, read through its manifest descriptors.

    The immutable generationPath is used rather than the compatibility mirror, which a later
    generation may already have replaced. Each artifact is checked against its descriptor.
    """
    root = Path(event['generationPath']).resolve()
    manifest_path = Path(event['manifestPath']).resolve()
    if manifest_path != root / 'manifest.json':
        raise RuntimeError('Watch event manifest is outside its generation.')
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    if manifest.get('generation', {}).get('id') != event['generationId']:
        raise RuntimeError('Watch event and manifest disagree on the generation.')
    artifacts = {artifact['relativePath']: artifact for artifact in manifest.get('artifacts', [])}
    def resolve(relative):
        artifact = artifacts.get(relative)
        if artifact is None:
            return None
        path = (root / relative).resolve()
        if root not in path.parents:
            raise RuntimeError(f'Unsafe artifact path: {relative}')
        data = path.read_bytes()
        if len(data) != artifact.get('byteSize') or hashlib.sha256(data).hexdigest() != str(artifact.get('checksum', '')).lower():
            raise RuntimeError(f'Artifact does not match its manifest: {relative}')
        return path
    source, lines = resolve('concatenated/source.ink'), resolve('lines/lines.json')
    if not source or not lines:
        raise RuntimeError('Generation has no concatenated/source.ink or lines/lines.json to import.')
    resolve('compiled/story.json')  # Verified when present; the importer reads it from MirrorRoot.
    return [f'-MirrorRoot={root}', f'-SourceInk={source}', f'-SourceLines={lines}']


class WatchImporter:
    """Imports the newest generation-completed event from each batch of watch output.

    Error, blocked and status events are reported by the CLI and never imported. A generation
    is accepted only after its import succeeds, so the repeat that bootstrap and reconnect send
    retries a failed import; an already accepted generation is skipped.
    """
    def __init__(self, importer=None):
        self.importer = importer or run_import
        self.accepted = {}

    def __call__(self, lines):
        latest = {}
        for line in lines:
            try:
                event = json.loads(line)
            except ValueError:
                continue
            if not isinstance(event, dict):
                continue
            if 'schemaVersion' not in event and event.get('status') == 'error':
                # A fatal CLI error under --json is a one-off sync result, not an NDJSON event;
                # the CLI has printed it and exits with its own code.
                continue
            if event.get('schemaVersion') != WATCH_SCHEMA_VERSION:
                print(f'Unsupported watch event schemaVersion {event.get("schemaVersion")!r}; update prosetta.py.', file=sys.stderr)
                return 1
            if event.get('type') == 'generation-completed':
                latest[(event.get('scope'), event.get('channel'))] = event
        for namespace, event in latest.items():
            if self.accepted.get(namespace) == event.get('generationId'):
                continue
            try:
                code = self.importer(generation_import_args(event))
            except (OSError, ValueError, KeyError, TypeError, AttributeError, RuntimeError) as error:
                # A malformed event or manifest skips that generation; it never ends the watch.
                print(f'Generation {event.get("generationId")} not imported: {error}', file=sys.stderr)
                continue
            if code:
                print(f'Generation {event.get("generationId")} import failed ({code}); previous assets kept.', file=sys.stderr)
                continue
            self.accepted[namespace] = event.get('generationId')
        return 0


class WatchPublisher:
    """Atomic, credential-free handoff to Unreal's DirectoryWatcher, never the mirror."""
    def __init__(self, directory=None):
        self.directory = directory or ROOT / 'Saved/ProsettaSync/Watch'

    def __call__(self, lines):
        for line in lines:
            try:
                event = json.loads(line)
                if not isinstance(event, dict) or event.get('type') != 'generation-completed':
                    continue
                if event.get('schemaVersion') != WATCH_SCHEMA_VERSION:
                    raise RuntimeError('Unsupported completed-generation schema.')
                generation_import_args(event)
                # Never persist arbitrary CLI fields (diagnostics can contain credentials).
                clean = {key: event[key] for key in (
                    'schemaVersion', 'type', 'projectId', 'scope', 'channel', 'generationId',
                    'generationPath', 'manifestPath')}
                revision = event.get('revision')
                if isinstance(revision, dict):
                    clean['revision'] = {k: v for k, v in revision.items()
                                         if (k == '*' or re.fullmatch(r'[0-9a-fA-F-]{36}', k))
                                         and isinstance(v, str) and v.isdigit()}
                release = event.get('release')
                if isinstance(release, dict):
                    clean['release'] = {k: v for k, v in release.items()
                                        if k in ('id', 'publishedAt', 'tag') and isinstance(v, str)}
                identity = json.dumps([clean['projectId'], clean['scope'], clean['channel']])
                name = hashlib.sha256(identity.encode()).hexdigest() + '.json'
                self.directory.mkdir(parents=True, exist_ok=True)
                with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', dir=self.directory,
                                                 suffix='.tmp', delete=False) as output:
                    temporary = Path(output.name)
                    try:
                        json.dump(clean, output)
                        output.close()
                        os.replace(temporary, self.directory / name)
                    finally:
                        # Also on a failed write or SIGTERM: no orphan .tmp in the watched directory.
                        output.close()  # Windows cannot unlink an open file.
                        temporary.unlink(missing_ok=True)
            except (OSError, ValueError, KeyError, TypeError, AttributeError, RuntimeError) as error:
                # Only the exception's own message: the event line and its fields can carry credentials.
                cause = ' '.join(str(error).split())
                cause = cause if len(cause) <= 200 else cause[:197] + '...'
                print('Completed generation could not be handed to Unreal; previous receipt retained. '
                      f'({type(error).__name__}{": " + cause if cause else ""})', file=sys.stderr)
        return 0


def main(args=None):
    args = list(sys.argv[1:] if args is None else args) or ['sync']
    command = args.pop(0)
    # Python's default SIGTERM exit skips cleanup, which would orphan the CLI the editor means to stop.
    previous = signal.signal(signal.SIGTERM, raise_terminated) if hasattr(signal, 'SIGTERM') else None
    try:
        if command in ('help', '-h', '--help'):
            print('Usage: sync-prosetta sync|verify|whoami|volumes|build|config|init|credentials|import|sync-import|native-bindings|watch|watch-import|relay|version [arguments]')
            return 0
        if command in ('version', '--version'):
            return subprocess.call([*tool_command(), '--version'])
        if command == 'import':
            return run_import(args)
        if command == 'relay':
            return run_relay(args)
        if command == 'native-bindings':
            copy_native_bindings()
            return 0
        if command == 'credentials':
            report = describe_credentials()
            if '--json' in args:
                print(json.dumps(report))
            else:
                print(f"Credential source: {report['source']} (local config: {report['localConfigPath']})")
                print(f"Project: {report['projectName'] or report['projectIdentifier'] or report['projectId'] or '-'} at {report['baseUrl']}; volumes: {report['volumes'] or 'all'}")
                for entry in report['candidates']:
                    state = 'token' if entry['hasToken'] else ('present' if entry['exists'] else 'absent')
                    print(f"  {entry['kind']:<6} {state:<7} {entry['path']}" + (f"  ({entry['warning']})" if entry['warning'] else ''))
            return 0
        # Do not allow a passthrough config to bypass project-local isolation (sync included).
        if '--config' in args or any(x.startswith('--config=') for x in args):
            raise RuntimeError('Wrappers manage --config; invoke the CLI directly for another config.')
        if command == 'sync':
            code = run_cli(['sync', *args])
            if code == 0: copy_native_bindings()
            return code
        if command == 'sync-import':
            code = run_cli(['sync'])
            if code == 0: copy_native_bindings()
            return code or run_import(args)
        if command not in ('sync', 'watch', 'watch-import', 'verify', 'whoami', 'volumes', 'build', 'config', 'init'):
            raise RuntimeError(f'Unknown command: {command}')
        if command == 'watch-import':
            return run_cli(watch_arguments(args), on_lines=WatchImporter())
        if command == 'watch':
            return run_cli(watch_arguments(args), on_lines=WatchPublisher())
        return run_cli([command, *args])
    except Terminated:
        return 143  # 128 + SIGTERM; the CLI has already been stopped.
    except (OSError, ValueError, RuntimeError) as error:
        print(f'Prosetta wrapper failed: {error}', file=sys.stderr)
        return 1
    finally:
        if hasattr(signal, 'SIGTERM'):
            signal.signal(signal.SIGTERM, signal.SIG_DFL if previous is None else previous)


if __name__ == '__main__':
    sys.exit(main())
