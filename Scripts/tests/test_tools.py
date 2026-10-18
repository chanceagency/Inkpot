import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

SCRIPTS = Path(__file__).resolve().parents[1]

def load(name):
    spec = importlib.util.spec_from_file_location(name, SCRIPTS / f'{name}.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

ffmpeg = load('ffmpeg')
prosetta = load('prosetta')


class ProjectLayoutTests(unittest.TestCase):
    """The plugin copy of the wrapper serves whichever project it is installed in."""

    def make_project(self, root, modules):
        root.mkdir(parents=True, exist_ok=True)
        (root / 'Game.uproject').write_text(json.dumps({'FileVersion': 3, 'Modules': [{'Name': name} for name in modules]}))
        return root

    def test_project_root_prefers_environment(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            self.assertEqual(prosetta.project_root({'PROSETTA_PROJECT_ROOT': str(root)}), root)

    def test_project_root_finds_uproject_above_plugin(self):
        with tempfile.TemporaryDirectory() as temp:
            root = self.make_project(Path(temp).resolve() / 'Game', ['Game'])
            script = root / 'Plugins' / 'Inkpot' / 'Scripts' / 'prosetta.py'
            script.parent.mkdir(parents=True)
            self.assertEqual(prosetta.project_root({}, script), root)
            self.assertEqual(prosetta.project_root({}, root / 'Scripts' / 'prosetta.py'), root)

    def test_native_bindings_go_to_first_module(self):
        with tempfile.TemporaryDirectory() as temp:
            root = self.make_project(Path(temp) / 'Game', ['Game', 'GameEditor'])
            self.assertEqual(prosetta.native_bindings_destination(root, {}), root / 'Source/Game/Prosetta/NativeBindings')
            self.assertEqual(prosetta.native_bindings_destination(root, {'PROSETTA_NATIVE_BINDINGS_DIR': 'Source/Other'}), root / 'Source/Other')
            blueprint_only = self.make_project(Path(temp) / 'Blueprints', [])
            self.assertIsNone(prosetta.native_bindings_destination(blueprint_only, {}))

    def test_bindings_without_module_are_reported_not_copied(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / 'cpp'
            source.mkdir()
            (source / 'A.cpp').write_text('// x')
            lines = []
            self.assertEqual(prosetta.copy_native_bindings(source, None, Path(temp) / 'manifest.json', lines.append), ([], []))
            self.assertIn('PROSETTA_NATIVE_BINDINGS_DIR', lines[0])

    def test_missing_helper_names_the_script(self):
        with tempfile.TemporaryDirectory() as temp, patch.object(prosetta, 'ROOT', Path(temp)), \
                patch.object(prosetta, 'helper_script', return_value=None):
            with self.assertRaisesRegex(RuntimeError, 'import-prosetta.py'):
                prosetta.run_import([])


class PlatformTests(unittest.TestCase):
    def test_platform_selection(self):
        for system, machine, key in [('Darwin','arm64','mac-arm64'), ('Darwin','x86_64','mac-x86_64'),
                                     ('Windows','AMD64','windows-x86_64'), ('Linux','aarch64','linux-arm64'),
                                     ('Linux','x86_64','linux-x86_64')]:
            self.assertEqual(ffmpeg.platform_key(system,machine), key)
        with self.assertRaisesRegex(RuntimeError, 'No pinned'):
            ffmpeg.platform_key('Windows', 'ARM64')

    def test_checksum_mismatch_leaves_no_executable(self):
        with tempfile.TemporaryDirectory() as root:
            import io
            with patch.object(ffmpeg.urllib.request, 'urlopen', return_value=io.BytesIO(b'bad archive')):
                with self.assertRaisesRegex(RuntimeError, 'checksum mismatch'):
                    ffmpeg.resolve(root)
            self.assertFalse([p for p in Path(root).rglob('*') if p.is_file() and p.name in ('ffmpeg.exe', 'ffmpeg')])

    def test_missing_offline_cache(self):
        with tempfile.TemporaryDirectory() as root:
            with self.assertRaisesRegex(RuntimeError, 'No verified cached'):
                ffmpeg.resolve(root, offline=True)


class CredentialTests(unittest.TestCase):
    def test_native_cli_config_after_home_config(self):
        with tempfile.TemporaryDirectory() as directory:
            home=Path(directory)/'home.json'; native=Path(directory)/'native.json'
            native.write_text(json.dumps({'token':'native','projectId':24}))
            with patch.object(prosetta,'HOME',home), patch.object(prosetta,'native_config_paths',return_value=[native]):
                self.assertEqual(prosetta.fallback_config()['token'],'native')
                home.write_text(json.dumps({'token':'home'}))
                self.assertEqual(prosetta.fallback_config()['token'],'home')
                home.write_text(json.dumps({'projectId':24}))
                self.assertEqual(prosetta.fallback_config()['token'],'native')

    def test_local_precedence_does_not_read_home(self):
        local = {'projectId': 24, 'token': 'local', 'volumes': 'demo'}
        settings,token = prosetta.select_credentials(local, home_loader=lambda: self.fail('home read'), environment={}, check=lambda s,t: None)
        self.assertEqual(token,'local'); self.assertEqual(settings,local)

    def test_expired_local_uses_only_home_token(self):
        local = {'projectId':24,'baseUrl':'https://local.example','volumes':'demo','token':'expired'}
        home = {'projectId':42,'baseUrl':'https://other.example','volumes':'other','token':'home'}
        checks=[]
        def check(s,t):
            checks.append((dict(s),t))
            if t == 'expired': raise prosetta.ExpiredToken()
        settings,token = prosetta.select_credentials(local,lambda:home,{},check)
        self.assertEqual(token,'home'); self.assertEqual(settings,local)
        self.assertEqual(checks[-1][0]['projectId'],24)
        self.assertEqual(checks[-1][0]['volumes'],'demo')

    def test_missing_local_token_falls_back(self):
        self.assertEqual(prosetta.select_credentials({'projectId':24},lambda:{'token':'home'}, {}, lambda s,t: None)[1], 'home')

    def test_explicit_environment_never_falls_back(self):
        with self.assertRaises(prosetta.ExpiredToken):
            prosetta.select_credentials({'token':'local'},lambda:self.fail('home read'), {'PROSETTA_PROJECT_TOKEN':'explicit'}, lambda s,t: (_ for _ in ()).throw(prosetta.ExpiredToken()))

    def test_network_error_never_falls_back(self):
        with self.assertRaisesRegex(OSError,'network'):
            prosetta.select_credentials({'token':'local'},lambda:self.fail('home read'), {}, lambda s,t: (_ for _ in ()).throw(OSError('network')))

    def test_home_only_full_config_and_environment_scope(self):
        settings,token=prosetta.select_credentials({},lambda:{'projectId':24,'token':'home','volumes':'demo'}, {'PROSETTA_PROJECT_ID':'25'}, lambda s,t: None)
        self.assertEqual(settings['projectId'],25); self.assertEqual(settings['volumes'],'demo')

    def test_home_token_requires_local_project_identity(self):
        with self.assertRaisesRegex(RuntimeError, 'no project identity'):
            prosetta.select_credentials({'volumes':'demo'},lambda:{'token':'home'}, {}, lambda s,t: None)

    def test_wrong_project_rejected(self):
        import io
        with patch.object(prosetta.urllib.request,'urlopen',return_value=io.BytesIO(b'{"projectId":42}')):
            with self.assertRaisesRegex(RuntimeError, 'different project'):
                prosetta.introspect({'projectId':24},'home')

    def test_only_exact_expired_response_allows_fallback(self):
        import io
        import urllib.error
        for status, body, exception in [(403, b'{"code":"FORBIDDEN"}', RuntimeError),
                                        (500, b'{"code":"INVALID_OR_EXPIRED_TOKEN"}', RuntimeError),
                                        (401, b'{"code":"INVALID_OR_EXPIRED_TOKEN"}', prosetta.ExpiredToken)]:
            error = urllib.error.HTTPError('https://example',status,'error',{},io.BytesIO(body))
            with patch.object(prosetta.urllib.request,'urlopen',side_effect=error):
                with self.assertRaises(exception) as raised:
                    prosetta.introspect({'projectId':24},'local')
                if exception is RuntimeError:
                    self.assertNotIsInstance(raised.exception,prosetta.ExpiredToken)

    def test_shared_home_file_is_not_written(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); local=root/'.prosetta-sync/config.json'; home=root/'home.json'
            local.parent.mkdir(); local.write_text(json.dumps({'projectId':24,'token':'expired','volumes':'demo'}))
            home.write_text(json.dumps({'token':'home','projectId':42}))
            before=home.read_bytes()
            def select(settings, **kwargs):
                return dict(settings), 'home'
            def command(args,env):
                config=Path(args[args.index('--config')+1]); data=json.loads(config.read_text())
                self.assertEqual(env['PROSETTA_PROJECT_TOKEN'],'home')
                self.assertEqual(data['projectId'],24); self.assertEqual(data['volumes'],'demo')
                data['token']='home'; config.write_text(json.dumps(data))
                return 0
            with patch.multiple(prosetta, ROOT=root, LOCAL=local, HOME=home, LEGACY=root/'legacy.json'), patch.object(prosetta,'select_credentials',select), patch.object(prosetta,'run_foreground',command):
                self.assertEqual(prosetta.run_cli(['sync']),0)
            self.assertEqual(home.read_bytes(),before)
            self.assertEqual(json.loads(local.read_text())['token'],'expired')


    def test_plain_http_only_for_loopback(self):
        for base in ['http://localhost:3000', 'http://127.0.0.1:3000/', 'http://[::1]:3000', 'http://prosetta.localhost']:
            prosetta.require_secure_base(base)
        for base in ['http://prosetta.studio', 'http://localhost.evil.example', 'ftp://localhost', 'http://10.0.0.5:3000']:
            with self.assertRaisesRegex(RuntimeError, 'HTTPS'):
                prosetta.require_secure_base(base)
        import io
        with patch.object(prosetta.urllib.request,'urlopen',return_value=io.BytesIO(b'{"projectId":2}')) as opened:
            prosetta.introspect({'baseUrl':'http://localhost:3000','projectId':2},'dev')
        self.assertEqual(opened.call_args[0][0].full_url,'http://localhost:3000/api/engine-sync/v1/project-token-introspection')

    def test_dev_server_cache_does_not_share_production_key(self):
        production = prosetta.cache_path({'projectId': 2, 'baseUrl': 'https://prosetta.studio'})
        local = prosetta.cache_path({'projectId': 2, 'baseUrl': 'http://localhost:3000'})
        self.assertNotEqual(production, local)
        self.assertEqual(local.name, 'localhost-3000-2')
        self.assertEqual(production.name, '2')

    def test_session_override_is_not_persisted(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); local=root/'.prosetta-sync/config.json'
            local.parent.mkdir()
            original={'baseUrl':'https://prosetta.studio','projectId':23,'token':'production'}
            local.write_text(json.dumps(original))
            before=local.read_bytes()
            def command(args,env):
                config=Path(args[args.index('--config')+1]); data=json.loads(config.read_text())
                self.assertEqual(data['baseUrl'],'http://localhost:3000'); self.assertEqual(data['projectId'],2)
                self.assertEqual(env['PROSETTA_PROJECT_TOKEN'],'dev')
                self.assertTrue(data['cachePath'].endswith('localhost-3000-2'))
                data['lastSync']='now'; config.write_text(json.dumps(data))
                return 0
            environment={'PROSETTA_PROJECT_TOKEN':'dev','PROSETTA_BASE_URL':'http://localhost:3000','PROSETTA_PROJECT_ID':'2'}
            with patch.multiple(prosetta, ROOT=root, LOCAL=local, LEGACY=root/'legacy.json'), \
                 patch.dict(prosetta.os.environ, environment), \
                 patch.object(prosetta.urllib.request,'urlopen',side_effect=lambda *a,**k: __import__('io').BytesIO(b'{"projectId":2}')), \
                 patch.object(prosetta,'run_foreground',command):
                self.assertEqual(prosetta.run_cli(['sync','--json']),0)
            self.assertEqual(local.read_bytes(),before)

    def test_local_commands_follow_session_override(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); local=root/'.prosetta-sync/config.json'
            local.parent.mkdir(); local.write_text(json.dumps({'baseUrl':'https://prosetta.studio','projectId':23,'token':'production'}))
            before=local.read_bytes(); seen={}
            def command(args,env):
                seen.update(json.loads(Path(args[args.index('--config')+1]).read_text()))
                return 0
            with patch.multiple(prosetta, ROOT=root, LOCAL=local, LEGACY=root/'legacy.json'), \
                 patch.dict(prosetta.os.environ, {'PROSETTA_BASE_URL':'http://localhost:3000','PROSETTA_PROJECT_ID':'2'}), \
                 patch.object(prosetta,'run_foreground',command):
                self.assertEqual(prosetta.run_cli(['verify']),0)
            self.assertTrue(seen['cachePath'].endswith('localhost-3000-2'))
            self.assertEqual(local.read_bytes(),before)

    def test_token_stdin_never_in_argv(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); local=root/'.prosetta-sync/config.json'
            seen={}
            def command(args,env):
                seen['args']=list(args); seen['token']=env.get('PROSETTA_PROJECT_TOKEN')
                config=Path(args[args.index('--config')+1]); data=json.loads(config.read_text())
                data['projectId']=2; config.write_text(json.dumps(data))
                return 0
            with patch.multiple(prosetta, ROOT=root, LOCAL=local, LEGACY=root/'legacy.json'), \
                 patch.dict(prosetta.os.environ, {}, clear=False), \
                 patch.object(prosetta.sys,'stdin',io.StringIO('secret-token\n')), \
                 patch.object(prosetta.urllib.request,'urlopen',side_effect=lambda *a,**k: io.BytesIO(b'{"projectId":2}')), \
                 patch.object(prosetta,'run_foreground',command):
                prosetta.os.environ.pop('PROSETTA_PROJECT_TOKEN', None)
                self.assertEqual(prosetta.run_cli(['init','--token-stdin','--project-id','2','--base-url','http://localhost:3000']),0)
            self.assertNotIn('--token-stdin', seen['args'])
            self.assertFalse(any('secret-token' in a for a in seen['args']))
            self.assertEqual(seen['token'],'secret-token')
            self.assertIn('--project-id', seen['args'])
            saved=json.loads(local.read_text())
            self.assertEqual(saved['token'],'secret-token')
            if os.name == 'posix': self.assertEqual(oct(local.stat().st_mode & 0o777),'0o600')

    def test_init_checks_the_token_against_the_new_project(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); local=root/'.prosetta-sync/config.json'
            local.parent.mkdir(); local.write_text(json.dumps({'baseUrl':'http://localhost:3000','projectId':23,'token':'old'}))
            # The token belongs to project 2; introspection fails with "different project" if it is checked against 23.
            with patch.multiple(prosetta, ROOT=root, LOCAL=local, LEGACY=root/'legacy.json'), \
                 patch.dict(prosetta.os.environ, {}), \
                 patch.object(prosetta.sys,'stdin',io.StringIO('new-token\n')), \
                 patch.object(prosetta.urllib.request,'urlopen',side_effect=lambda *a,**k: io.BytesIO(b'{"projectId":2}')), \
                 patch.object(prosetta,'run_foreground',lambda args,env: 0):
                for key in ('PROSETTA_PROJECT_TOKEN','PROSETTA_PROJECT_ID'): prosetta.os.environ.pop(key, None)
                self.assertEqual(prosetta.run_cli(['init','--token-stdin','--project-id','2','--base-url','http://localhost:3000']),0)
                with self.assertRaisesRegex(RuntimeError,'numeric'):
                    prosetta.run_cli(['init','--project-id=abc'])

    def test_token_stdin_requires_a_line(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); local=root/'.prosetta-sync/config.json'
            with patch.multiple(prosetta, ROOT=root, LOCAL=local, LEGACY=root/'legacy.json'), \
                 patch.object(prosetta.sys,'stdin',io.StringIO('')), \
                 patch.object(prosetta,'run_foreground',lambda *a,**k: self.fail('CLI must not run')):
                with self.assertRaisesRegex(RuntimeError,'token-stdin'):
                    prosetta.run_cli(['init','--token-stdin'])

    def test_session_flags_stripped_and_not_persisted(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); local=root/'.prosetta-sync/config.json'
            local.parent.mkdir(); local.write_text(json.dumps({'baseUrl':'https://prosetta.studio','projectId':23,'token':'production'}))
            before=local.read_bytes(); seen={}
            def command(args,env):
                seen['args']=list(args); seen.update(json.loads(Path(args[args.index('--config')+1]).read_text()))
                return 0
            with patch.multiple(prosetta, ROOT=root, LOCAL=local, LEGACY=root/'legacy.json'), \
                 patch.dict(prosetta.os.environ, {}), \
                 patch.object(prosetta,'run_foreground',command):
                for key in ('PROSETTA_BASE_URL','PROSETTA_PROJECT_ID'): prosetta.os.environ.pop(key, None)
                self.assertEqual(prosetta.run_cli(['verify','--base-url','http://localhost:3000','--project-id=2']),0)
            self.assertNotIn('--base-url', seen['args']); self.assertNotIn('--project-id', seen['args'])
            self.assertFalse(any(a.startswith('--project-id=') for a in seen['args']))
            self.assertEqual(seen['baseUrl'],'http://localhost:3000'); self.assertEqual(seen['projectId'],2)
            self.assertTrue(seen['cachePath'].endswith('localhost-3000-2'))
            self.assertEqual(local.read_bytes(),before)
            with patch.multiple(prosetta, ROOT=root, LOCAL=local, LEGACY=root/'legacy.json'), \
                 patch.object(prosetta,'run_foreground',command):
                with self.assertRaisesRegex(RuntimeError,'numeric'):
                    prosetta.run_cli(['verify','--project-id','abc'])

    def test_credentials_json_reports_source_without_token(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); local=root/'.prosetta-sync/config.json'; home=root/'home.json'
            local.parent.mkdir()
            local.write_text(json.dumps({'baseUrl':'https://prosetta.studio','projectId':23,'projectName':'Demo','token':'secret-token','volumes':'a'}))
            local.chmod(0o644)
            common=dict(ROOT=root, LOCAL=local, LEGACY=root/'legacy.json', HOME=home, native_config_paths=lambda: [root/'native.json'])
            with patch.multiple(prosetta, **common), patch.dict(prosetta.os.environ, {}):
                for key in ('PROSETTA_PROJECT_TOKEN','PROSETTA_BASE_URL','PROSETTA_PROJECT_ID'): prosetta.os.environ.pop(key, None)
                report=prosetta.describe_credentials()
                text=json.dumps(report)
                self.assertNotIn('secret-token', text); self.assertNotIn('"token"', text)
                self.assertEqual(report['source'],'local'); self.assertEqual(report['projectId'],23)
                self.assertEqual(report['projectName'],'Demo'); self.assertEqual(report['volumes'],'a')
                self.assertFalse(report['sessionOverride'])
                first=report['candidates'][0]
                self.assertTrue(first['hasToken'])
                if os.name == 'posix': self.assertEqual(first['mode'],'644'); self.assertIn('chmod 600', first['warning'])
                else: self.assertIsNone(first['mode']); self.assertIsNone(first['warning'])
                self.assertEqual([c['kind'] for c in report['candidates']],['local','local','home','native'])
                self.assertEqual(Path(report['mirrorPath']), prosetta.ROOT / 'Saved' / 'Prosetta')
                self.assertIn('available', report['cli'])
                stdout=io.StringIO()
                with patch.object(prosetta.sys,'stdout',stdout):
                    self.assertEqual(prosetta.main(['credentials','--json']),0)
                self.assertEqual(json.loads(stdout.getvalue())['source'],'local')
                self.assertNotIn('secret-token', stdout.getvalue())
                local.chmod(0o600)
                self.assertIsNone(prosetta.describe_credentials()['candidates'][0]['warning'])
            with patch.multiple(prosetta, **common), patch.dict(prosetta.os.environ, {'PROSETTA_PROJECT_TOKEN':'env-token','PROSETTA_BASE_URL':'http://localhost:3000'}):
                report=prosetta.describe_credentials()
                self.assertEqual(report['source'],'env'); self.assertTrue(report['sessionOverride'])
                self.assertEqual(report['baseUrl'],'http://localhost:3000')
                self.assertNotIn('env-token', json.dumps(report))
            local.unlink()
            with patch.multiple(prosetta, **common), patch.dict(prosetta.os.environ, {}):
                prosetta.os.environ.pop('PROSETTA_PROJECT_TOKEN', None)
                report=prosetta.describe_credentials()
                self.assertEqual(report['source'],'none'); self.assertFalse(report['localConfigExists'])
                self.assertEqual(report['baseUrl'], prosetta.DEFAULT_BASE_URL)

class NativeBindingCopyTests(unittest.TestCase):
    def test_copies_changed_files_and_reports_stale_prosetta_files(self):
        with tempfile.TemporaryDirectory() as root:
            source=Path(root)/'mirror'; dest=Path(root)/'module'; source.mkdir(); dest.mkdir()
            (source/'Inventory.cpp').write_text('namespace Prosetta::Bindings::Inventory { int f() { return 1; } }\n')
            (source/'Inventory.Bindings.cpp').write_text('// Generated by Inkpot.\n// inkpot-native-binding module=Inventory source=prosetta\n')
            (source/'notes.txt').write_text('ignored')
            (dest/'SandboxLocal.cpp').write_text('namespace Prosetta::Bindings::SandboxLocal {}\n')
            (dest/'Old.Bindings.cpp').write_text('// Generated by Inkpot.\n// inkpot-native-binding module=Old source=prosetta\n')
            (dest/'Local.Bindings.cpp').write_text('// Generated by Inkpot.\n// inkpot-native-binding module=Local source=local\n')
            lines=[]
            copied,stale=prosetta.copy_native_bindings(source,dest,Path(root)/'manifest.json',out=lines.append)
            self.assertEqual(copied,['Inventory.Bindings.cpp','Inventory.cpp'])
            self.assertEqual(stale,['Old.Bindings.cpp'])
            self.assertTrue((dest/'Old.Bindings.cpp').exists(), 'stale files are reported, not deleted')
            self.assertFalse((dest/'notes.txt').exists())
            self.assertTrue((dest/'SandboxLocal.cpp').exists(), 'hand-written local module untouched')
            self.assertTrue(any('Prosetta.NativeBindings.Generate' in line for line in lines))
            copied,stale=prosetta.copy_native_bindings(source,dest,Path(root)/'manifest.json',out=lines.append)
            self.assertEqual(copied,[], 'unchanged mirror copies nothing')

    def test_missing_mirror_is_a_noop(self):
        with tempfile.TemporaryDirectory() as root:
            lines=[]
            self.assertEqual(prosetta.copy_native_bindings(Path(root)/'absent',Path(root)/'module',Path(root)/'m.json',out=lines.append),([],[]))
            self.assertFalse((Path(root)/'module').exists())

    def test_sync_runs_copy_after_success(self):
        calls=[]
        with patch.object(prosetta,'run_cli',side_effect=lambda args:(calls.append(args) or 0)), \
             patch.object(prosetta,'copy_native_bindings',side_effect=lambda **kw: calls.append('copy') or ([],[])):
            self.assertEqual(prosetta.main(['sync','--json']),0)
        self.assertEqual(calls,[['sync','--json'],'copy'])
        calls.clear()
        with patch.object(prosetta,'run_cli',return_value=3), patch.object(prosetta,'copy_native_bindings',side_effect=AssertionError('copied after failure')):
            self.assertEqual(prosetta.main(['sync']),3)


def write_generation(root, generation_id='a'*64, files=None):
    """A completed generation directory and its watch event, shaped like CLI 0.10.0 output."""
    import hashlib
    files = files or {'concatenated/source.ink': b'Hello {2+2}\n', 'lines/lines.json': b'{"lines":[]}',
                      'compiled/story.json': b'{"inkVersion":21,"root":[]}'}
    generation = Path(root) / generation_id
    artifacts = []
    for relative, data in files.items():
        path = generation / relative; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)
        artifacts.append({'key': relative.split('/')[0], 'kind': 'json', 'relativePath': relative,
                          'checksum': hashlib.sha256(data).hexdigest(), 'checksumAlgorithm': 'sha256', 'byteSize': len(data)})
    manifest = generation / 'manifest.json'
    manifest.write_text(json.dumps({'schemaVersion': 2, 'projectId': 42, 'generation': {'id': generation_id}, 'artifacts': artifacts}))
    return {'schemaVersion': 1, 'type': 'generation-completed', 'generationId': generation_id, 'generationPath': str(generation),
            'manifestPath': str(manifest), 'projectId': 42, 'scope': 'core', 'channel': 'live', 'changedKeys': [], 'removedKeys': [], 'code': None}


class WatchTests(unittest.TestCase):
    def test_watch_uses_native_cli_watch(self):
        calls = []
        with patch.object(prosetta, 'run_cli', lambda args, **kwargs: calls.append((args, kwargs)) or 0):
            self.assertEqual(prosetta.main(['watch', '--channel', 'latest-release', '--volumes', 'core']), 0)
            self.assertEqual(prosetta.main(['watch', '10', '--no-mirror']), 0)  # Pre-0.10 interval is dropped.
            self.assertEqual(prosetta.main(['watch-import']), 0)
            self.assertEqual(prosetta.main(['watch', '--config', 'other.json']), 1)
        self.assertEqual(calls[0][0], ['watch', '--channel', 'latest-release', '--volumes', 'core'])
        self.assertIsInstance(calls[0][1]['on_lines'], prosetta.WatchPublisher)
        self.assertEqual(calls[1][0], ['watch', '--no-mirror'])
        self.assertIsInstance(calls[1][1]['on_lines'], prosetta.WatchPublisher)
        self.assertEqual(calls[2][0], ['watch'])
        self.assertIsInstance(calls[2][1]['on_lines'], prosetta.WatchImporter)
        self.assertEqual(len(calls), 3)

    def test_publisher_hands_only_verified_sanitized_completions_to_editor(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); output = root / 'receipts'
            event = write_generation(root)
            event['token'] = 'SECRET'
            event['revision'] = {'*': '2804', '58f82c8a-faa6-48a7-a5b8-611a70345033': '2806', 'token': 'SECRET'}
            publish = prosetta.WatchPublisher(output)
            publish(['not json', json.dumps({'schemaVersion': 1, 'type': 'blocked'}), json.dumps(event)])
            receipts = list(output.glob('*.json'))
            self.assertEqual(len(receipts), 1)
            self.assertNotIn('SECRET', receipts[0].read_text())
            self.assertEqual(json.loads(receipts[0].read_text())['revision'], {'*': '2804', '58f82c8a-faa6-48a7-a5b8-611a70345033': '2806'})
            self.assertEqual(json.loads(receipts[0].read_text())['generationId'], event['generationId'])
            previous = receipts[0].read_bytes()
            (Path(event['generationPath'])/'compiled/story.json').write_text('tampered')
            publish([json.dumps(event)])
            self.assertEqual(receipts[0].read_bytes(), previous)
            next_event = write_generation(root, 'b'*64)
            publish([json.dumps(next_event)])
            self.assertEqual(len(list(output.iterdir())), 1)
            self.assertEqual(json.loads(receipts[0].read_text())['generationId'], 'b'*64)
            with patch('sys.stderr', new_callable=io.StringIO) as errors:
                publish([json.dumps({**write_generation(root, 'c'*64), 'schemaVersion': 2, 'token': 'SECRET'})])
                missing = root / ('x' * 120) / ('y' * 120)
                publish([json.dumps({**event, 'generationPath': str(missing), 'manifestPath': str(missing / 'manifest.json')})])
            failures = errors.getvalue().splitlines()
            self.assertEqual(failures[0], 'Completed generation could not be handed to Unreal; previous receipt retained. '
                                          '(RuntimeError: Unsupported completed-generation schema.)')
            cause = failures[1].split('(FileNotFoundError: ', 1)[1]
            self.assertEqual((len(cause), cause[-4:]), (201, '...)'))  # Long causes are cut to 200 characters.
            self.assertNotIn('SECRET', errors.getvalue())
            self.assertEqual(json.loads(receipts[0].read_text())['generationId'], 'b'*64)

    def test_publisher_leaves_no_temporary_file_when_a_write_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); output = root / 'receipts'
            event = write_generation(root)
            publish = prosetta.WatchPublisher(output)
            publish([json.dumps(event)])
            previous = {path.name: path.read_bytes() for path in output.iterdir()}
            for error in (OSError, prosetta.Terminated):
                def fail(*args, error=error, **kwargs): raise error('write interrupted')
                with patch.object(prosetta.json, 'dump', fail), patch('sys.stderr', new_callable=io.StringIO):
                    if error is OSError:
                        publish([json.dumps(write_generation(root, 'b'*64))])
                    else:
                        with self.assertRaises(prosetta.Terminated):  # SIGTERM from Stop watch mid-write.
                            publish([json.dumps(write_generation(root, 'b'*64))])
                self.assertEqual({path.name: path.read_bytes() for path in output.iterdir()}, previous)

    def test_watch_resolves_credentials_like_sync(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); seen = {}
            def select(settings, **kwargs):
                seen['remote'] = True; return {'projectId': 42}, 'token'
            def command(args, env):
                seen['args'] = args[args.index('watch'):args.index('--config')]
                seen['token'] = env['PROSETTA_PROJECT_TOKEN']
                return 0
            with patch.multiple(prosetta, ROOT=root, LOCAL=root/'local.json', HOME=root/'home.json', LEGACY=root/'legacy.json'), \
                 patch.object(prosetta, 'select_credentials', select), patch.object(prosetta, 'run_foreground', command):
                self.assertEqual(prosetta.run_cli(['watch', '--volumes', 'core']), 0)
            self.assertEqual(seen, {'remote': True, 'args': ['watch', '--volumes', 'core'], 'token': 'token'})

    def test_importer_imports_completed_generation_from_its_immutable_path(self):
        with tempfile.TemporaryDirectory() as directory:
            event = write_generation(directory); imported = []
            importer = prosetta.WatchImporter(lambda args: imported.append(args) or 0)
            other = [json.dumps({'schemaVersion': 1, 'type': kind, 'projectId': 42, 'scope': 'core', 'channel': 'live', 'code': code})
                     for kind, code in [('status', None), ('error', 'STREAM_RECONNECTING'), ('blocked', 'REST_409')]]
            self.assertEqual(importer([*other, 'not json', json.dumps(event)]), 0)
            root = Path(event['generationPath']).resolve()
            self.assertEqual(imported, [[f'-MirrorRoot={root}', f'-SourceInk={root/"concatenated/source.ink"}', f'-SourceLines={root/"lines/lines.json"}']])
            self.assertEqual(importer([json.dumps(event)]), 0)  # Bootstrap/reconnect repeat is deduplicated.
            self.assertEqual(len(imported), 1)
            self.assertEqual(importer(other), 0)
            self.assertEqual(len(imported), 1)

    def test_importer_keeps_only_newest_generation_of_a_batch(self):
        with tempfile.TemporaryDirectory() as directory:
            first, second = write_generation(directory, 'a'*64), write_generation(directory, 'b'*64)
            imported = []
            importer = prosetta.WatchImporter(lambda args: imported.append(args[0]) or 0)
            self.assertEqual(importer([json.dumps(first), json.dumps(second)]), 0)
            self.assertEqual(imported, [f'-MirrorRoot={Path(second["generationPath"]).resolve()}'])

    def test_failed_import_is_retried_on_repeat(self):
        with tempfile.TemporaryDirectory() as directory:
            event = write_generation(directory); results = [3, 0]; calls = []
            importer = prosetta.WatchImporter(lambda args: calls.append(args) or results.pop(0))
            self.assertEqual(importer([json.dumps(event)]), 0)
            self.assertEqual(importer([json.dumps(event)]), 0)
            self.assertEqual(importer([json.dumps(event)]), 0)
            self.assertEqual(len(calls), 2)

    def test_importer_rejects_tampered_or_incomplete_generations(self):
        with tempfile.TemporaryDirectory() as directory:
            imported = []
            importer = prosetta.WatchImporter(lambda args: imported.append(args) or 0)
            tampered = write_generation(directory, 'c'*64)
            (Path(tampered['generationPath']) / 'lines/lines.json').write_text('{"lines":[1]}')
            missing = write_generation(directory, 'd'*64, {'compiled/story.json': b'{}'})
            outside = write_generation(directory, 'e'*64); outside['manifestPath'] = str(Path(directory) / 'manifest.json')
            mismatched = write_generation(directory, 'f'*64); mismatched['generationId'] = '0'*64
            pathless = {**write_generation(directory, '1'*64), 'scope': 'other', 'generationPath': None}
            self.assertEqual(importer([json.dumps(e) for e in (tampered, missing, outside, mismatched, pathless)]), 0)
            self.assertEqual(imported, [])
            self.assertEqual(importer([json.dumps({**tampered, 'schemaVersion': 2})]), 1)

    def test_importer_leaves_fatal_cli_error_to_the_cli(self):
        imported = []
        importer = prosetta.WatchImporter(lambda args: imported.append(args) or 0)
        self.assertEqual(importer([json.dumps({'status': 'error', 'error': 'Project token rejected.'})]), 0)
        self.assertEqual(imported, [])

    def test_importer_skips_malformed_events_without_stopping(self):
        with tempfile.TemporaryDirectory() as directory:
            imported = []
            importer = prosetta.WatchImporter(lambda args: imported.append(args) or 0)
            null_path = {**write_generation(directory, '1'*64), 'generationPath': None}
            odd_manifest = write_generation(directory, '2'*64)
            Path(odd_manifest['manifestPath']).write_text(json.dumps({'generation': 'x', 'artifacts': []}))
            self.assertEqual(importer([json.dumps(null_path)]), 0)
            self.assertEqual(importer([json.dumps(odd_manifest)]), 0)
            self.assertEqual(imported, [])

    def test_stream_cli_hands_lines_to_consumer_and_propagates_exit(self):
        script = 'import sys\nfor i in range(3): print(i, flush=True)\nsys.exit(5)'
        seen = []
        self.assertEqual(prosetta.stream_cli([sys.executable, '-c', script], dict(os.environ), lambda lines: seen.extend(lines) or 0), 5)
        self.assertEqual([line.strip() for line in seen], ['0', '1', '2'])
        endless = 'import time\nwhile True: print("x", flush=True); time.sleep(0.05)'
        self.assertEqual(prosetta.stream_cli([sys.executable, '-c', endless], dict(os.environ), lambda lines: 1), 1)

    def test_stream_cli_stops_idle_cli_when_consumer_raises(self):
        idle = 'import time\nprint("ready", flush=True)\ntime.sleep(60)'
        wrapper = (f'import os, sys\nsys.path.insert(0, {str(SCRIPTS)!r})\nimport prosetta\n'
                   'def consume(lines): raise TypeError("bad event")\n'
                   f'prosetta.stream_cli([sys.executable, "-c", {idle!r}], dict(os.environ), consume)')
        result = subprocess.run([sys.executable, '-c', wrapper], capture_output=True, text=True, timeout=20)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('TypeError: bad event', result.stderr)

    @unittest.skipUnless(hasattr(os, 'killpg'), 'sends a POSIX process-group SIGINT')
    def test_ctrl_c_stops_watch_without_a_traceback(self):
        cli = 'import signal, sys, time\nsignal.signal(signal.SIGINT, lambda *_: sys.exit(130))\nprint("ready", flush=True)\ntime.sleep(30)'
        runners = {
            'watch': f'prosetta.run_foreground([sys.executable, "-c", {cli!r}], dict(os.environ))',
            'watch-import': f'prosetta.stream_cli([sys.executable, "-c", {cli!r}], dict(os.environ), lambda lines: 0)',
        }
        for name, runner in runners.items():
            with self.subTest(name):
                wrapper = f'import os, sys\nsys.path.insert(0, {str(SCRIPTS)!r})\nimport prosetta\nsys.exit({runner})'
                process = subprocess.Popen([sys.executable, '-c', wrapper], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, start_new_session=True)
                self.assertEqual(process.stdout.readline().strip(), 'ready')
                os.killpg(process.pid, signal.SIGINT)  # What a terminal Ctrl+C does.
                _, errors = process.communicate(timeout=20)
                self.assertEqual(process.returncode, 130)
                self.assertNotIn('Traceback', errors)

    @unittest.skipUnless(os.name == 'posix', 'sends a POSIX SIGTERM')
    def test_sigterm_stops_wrapper_and_its_cli(self):
        # The editor stops `watch` with SIGTERM to the wrapper alone; the CLI must not outlive it.
        cli = 'import os, time\nprint(os.getpid(), flush=True)\ntime.sleep(60)'
        def alive(pid):
            try:
                os.kill(pid, 0)
            except ProcessLookupError:
                return False
            return True
        def kill(pid):
            if alive(pid): os.kill(pid, signal.SIGKILL)
        for command, runner in [('watch', 'stream_cli'), ('verify', 'run_foreground')]:
            with self.subTest(runner):
                wrapper = (f'import os, sys\nsys.path.insert(0, {str(SCRIPTS)!r})\nimport prosetta\n'
                           f'cli = [sys.executable, "-c", {cli!r}]\n'
                           'def run_cli(args, on_lines=None):\n'
                           '    if on_lines: return prosetta.stream_cli(cli, dict(os.environ), lambda lines: 0)\n'
                           '    return prosetta.run_foreground(cli, dict(os.environ))\n'
                           'prosetta.run_cli = run_cli\n'
                           f'sys.exit(prosetta.main([{command!r}]))')
                process = subprocess.Popen([sys.executable, '-c', wrapper], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                self.addCleanup(process.kill)  # No-op once it has exited.
                cli_pid = int(process.stdout.readline())
                self.addCleanup(kill, cli_pid)
                time.sleep(0.3)  # Let the wrapper settle into waiting on the idle CLI.
                process.send_signal(signal.SIGTERM)  # What TerminateProc does on macOS: the wrapper only.
                process.wait(timeout=10)
                deadline = time.monotonic() + 5
                while alive(cli_pid) and time.monotonic() < deadline:
                    time.sleep(0.05)
                self.assertFalse(alive(cli_pid), f'{runner} left the CLI running after SIGTERM')
                _, errors = process.communicate(timeout=10)
                self.assertEqual(process.returncode, 143)
                self.assertNotIn('Traceback', errors)



@unittest.skipUnless(os.environ.get('FFMPEG_DOWNLOAD_TEST') == '1', 'set FFMPEG_DOWNLOAD_TEST=1 for real download/conversion')
class DownloadTests(unittest.TestCase):
    def test_fresh_conversion_offline_reuse_override_and_corruption(self):
        with tempfile.TemporaryDirectory() as root:
            executable=ffmpeg.resolve(root)
            pcm=Path(root)/'tone.wav'; compressed=Path(root)/'tone.mp3'; converted=Path(root)/'decoded.wav'
            subprocess.run([str(executable),'-nostdin','-v','error','-f','lavfi','-i','sine=frequency=440:duration=0.2',str(compressed)],check=True)
            subprocess.run([str(executable),'-nostdin','-v','error','-i',str(compressed),'-c:a','pcm_s16le',str(converted)],check=True)
            self.assertTrue(converted.read_bytes().startswith(b'RIFF'))
            with patch.object(ffmpeg.urllib.request,'urlopen',side_effect=AssertionError('network during cached reuse')):
                self.assertEqual(ffmpeg.resolve(root,offline=True),executable)
                self.assertEqual(ffmpeg.resolve(root),executable)
                self.assertEqual(ffmpeg.resolve(root,override=str(executable)),executable)
            with executable.open('ab') as output: output.write(b'corruption')
            with self.assertRaisesRegex(RuntimeError,'No verified cached'):
                ffmpeg.resolve(root,offline=True)

if __name__=='__main__': unittest.main()
