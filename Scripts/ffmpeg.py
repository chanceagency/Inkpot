#!/usr/bin/env python3
"""Editor-only FFmpeg bootstrap. Uses Python stdlib; never installs packages."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import ssl
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

def project_root():
    """PROSETTA_PROJECT_ROOT, else the closest folder above this script that holds a .uproject."""
    if os.environ.get('PROSETTA_PROJECT_ROOT'):
        return Path(os.environ['PROSETTA_PROJECT_ROOT']).resolve()
    here = Path(__file__).resolve().parent
    return next((folder for folder in [here, *here.parents] if any(folder.glob('*.uproject'))), here.parent)


ROOT = project_root()
LOCK = json.loads(Path(__file__).with_name('ffmpeg-lock.json').read_text())


def platform_key(system=None, machine=None):
    system = system or platform.system()
    machine = (machine or platform.machine()).lower()
    arch = {'amd64': 'x86_64', 'x86_64': 'x86_64', 'arm64': 'arm64', 'aarch64': 'arm64'}.get(machine)
    name = {'Darwin': 'mac', 'Windows': 'windows', 'Linux': 'linux'}.get(system)
    key = f'{name}-{arch}'
    if key not in LOCK['platforms']:
        raise RuntimeError(f'No pinned decoder for {system}/{machine}; supply -AudioDecoder=/absolute/path/to/ffmpeg.')
    return key


def digest(path):
    # Chunked rather than hashlib.file_digest so a pre-3.11 system python3 still works.
    sha256 = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b''):
            sha256.update(chunk)
    return sha256.hexdigest()


def validate(path):
    path = Path(path).resolve()
    result = subprocess.run([str(path), '-version'], capture_output=True, text=True, timeout=20)
    if result.returncode or not result.stdout.startswith('ffmpeg version '):
        raise RuntimeError(f'Decoder cannot run: {path}\n{result.stderr}')
    return path


def resolve(root=ROOT, override=None, offline=False, ca_file=None):
    if override:
        candidate = Path(override).expanduser()
        if not candidate.is_file():
            found = shutil.which(override)
            if not found:
                raise RuntimeError(f'Explicit decoder not found: {override}')
            candidate = Path(found)
        return validate(candidate)
    key = platform_key()
    pin = LOCK['platforms'][key]
    cache = Path(root).resolve() / 'Saved' / 'Tools' / 'FFmpeg' / LOCK['version'] / key
    executable = cache / ('ffmpeg.exe' if key.startswith('windows') else 'ffmpeg')
    # Re-check the binary against the repository pin on every use, including offline.
    if executable.is_file() and digest(executable) == pin['binarySha256']:
        return validate(executable)
    if offline:
        raise RuntimeError(f'No verified cached decoder at {executable}. Run online once or supply --decoder.')
    cache.parent.mkdir(parents=True, exist_ok=True)
    print(f'Downloading pinned FFmpeg for {key} (first import only)...', file=sys.stderr)
    with tempfile.TemporaryDirectory(prefix=f'.{key}-', dir=cache.parent) as temporary:
        stage = Path(temporary)
        archive = stage / 'download.whl'
        request = urllib.request.Request(pin['url'], headers={'User-Agent': 'InkpotSandbox-FFmpegBootstrap/1'})
        with urllib.request.urlopen(request, timeout=60, context=ssl.create_default_context(cafile=ca_file)) as response, archive.open('wb') as output:
            shutil.copyfileobj(response, output)
        if digest(archive) != pin['sha256']:
            raise RuntimeError('FFmpeg download checksum mismatch; nothing installed.')
        output_dir = stage / 'install'
        output_dir.mkdir()
        staged_executable = output_dir / executable.name
        with zipfile.ZipFile(archive) as wheel:
            # Extract only reviewed members; never trust paths or execute package code.
            with wheel.open(pin['member']) as source, staged_executable.open('wb') as output:
                shutil.copyfileobj(source, output)
            for index, member in enumerate(wheel.namelist()):
                if 'license' in member.lower() or member.endswith('/METADATA'):
                    (output_dir / f'upstream-notice-{index}.txt').write_bytes(wheel.read(member))
        if digest(staged_executable) != pin['binarySha256']:
            raise RuntimeError('Extracted decoder checksum mismatch; nothing installed.')
        staged_executable.chmod(0o755)
        validate(staged_executable)
        for flag, filename in [('-L', 'FFmpeg-LICENSE.txt'), ('-version', 'FFmpeg-BUILD.txt')]:
            report = subprocess.run([str(staged_executable), flag], capture_output=True, text=True, check=True, timeout=20)
            (output_dir / filename).write_text(report.stdout + report.stderr)
        shutil.copyfile(Path(__file__).with_name('FFmpeg-NOTICES.md'), output_dir / 'FFmpeg-NOTICES.md')
        cache.mkdir(exist_ok=True)
        for member in output_dir.iterdir():
            target = cache / member.name
            # Concurrent imports may finish the same pin while this download runs.
            if target == executable and target.is_file() and digest(target) == pin['binarySha256']:
                continue
            os.replace(member, target)
    return validate(executable)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project', type=Path, default=ROOT)
    parser.add_argument('--decoder')
    parser.add_argument('--offline', action='store_true')
    parser.add_argument('--ca-file')
    args = parser.parse_args()
    try:
        print(resolve(args.project, args.decoder, args.offline, args.ca_file))
    except (OSError, RuntimeError, subprocess.SubprocessError, zipfile.BadZipFile) as error:
        print(f'FFmpeg setup failed: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
