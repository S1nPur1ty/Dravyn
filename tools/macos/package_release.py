#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Package a deployed macOS app for public distribution without local diagnostics."""

import argparse
import hashlib
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import tempfile


def package(bundle, binary, output, version, minimum_os):
    root = Path(__file__).resolve().parents[2]
    output.mkdir(parents=True, exist_ok=True)
    app = output / 'Dravyn.app'
    if app.exists():
        raise ValueError('Output app already exists; use a new output directory')
    subprocess.run(['ditto', str(bundle), str(app)], check=True)
    shutil.copy2(binary, app / 'Contents/MacOS/Dravyn')
    # A fresh executable still references the build machine's libraries. Deploy it
    # again even when the donor bundle already contains those dependencies.
    subprocess.run(['macdeployqt', str(app), '-no-codesign'], check=True)
    for file in app.rglob('*'):
        if not file.is_file() or file.is_symlink():
            continue
        with file.open('rb') as stream:
            magic = stream.read(4)
        if magic not in (b'\xcf\xfa\xed\xfe', b'\xfe\xed\xfa\xcf',
                         b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca'):
            continue
        libraries = subprocess.check_output(['otool', '-L', str(file)], text=True)
        for line in libraries.splitlines():
            # Universal binaries have a separate unindented header per architecture.
            if not line.startswith('\t'):
                continue
            dependency = line.strip().split(' (', 1)[0]
            if dependency.startswith('/') and not dependency.startswith(
                    ('/System/Library/', '/usr/lib/')):
                raise ValueError(f'Non-portable dependency in {file.name}: {dependency}')
    info_path = app / 'Contents/Info.plist'
    info = plistlib.loads(info_path.read_bytes())
    for key in ('LSEnvironment', 'MetalCaptureEnabled', 'CFBundleIconName'):
        info.pop(key, None)
    info.update(CFBundleName='Dravyn', CFBundleDisplayName='Dravyn',
                CFBundleExecutable='Dravyn', CFBundleIdentifier='dev.dravyn.emulator',
                CFBundleIconFile='dravyn.icns', CFBundleVersion=version,
                CFBundleShortVersionString=version, LSMinimumSystemVersion=minimum_os,
                EglantineProfile='dravyn')
    info_path.write_bytes(plistlib.dumps(info))
    resources = app / 'Contents/Resources'
    shutil.copy2(root / 'dist/dravyn.icns', resources / 'dravyn.icns')
    shutil.copy2(root / 'LICENSE.txt', resources / 'LICENSE.txt')
    shutil.copytree(root / 'LICENSES', resources / 'LICENSES', dirs_exist_ok=True)
    (resources / 'SOURCE.txt').write_text(
        f'Dravyn {version}\nSource and dependency build instructions:\n'
        f'https://github.com/S1nPur1ty/Dravyn/tree/v{version}\n'
        'Derived from Eglantine, Eden, Yuzu and Sudachi.\n')
    for file in app.rglob('*'):
        if file.suffix.lower() in ('.keys', '.nsp', '.xci', '.gputrace', '.ini', '.log'):
            raise ValueError(f'Unexpected runtime data in bundle: {file.relative_to(app)}')
    with tempfile.TemporaryDirectory(prefix='dravyn-release-') as directory:
        temporary = Path(directory)
        entitlements = temporary / 'entitlements.plist'
        entitlements.write_bytes(plistlib.dumps({'com.apple.security.cs.allow-jit': True}))
        subprocess.run(['codesign', '--force', '--sign', '-', '--entitlements',
                        str(entitlements), str(app)], check=True)
        subprocess.run(['codesign', '--verify', '--deep', '--strict', str(app)], check=True)
        stage = temporary / 'dmg'
        stage.mkdir()
        subprocess.run(['ditto', str(app), str(stage / app.name)], check=True)
        (stage / 'Applications').symlink_to('/Applications')
        dmg = output / 'Dravyn-macOS-arm64.dmg'
        subprocess.run(['hdiutil', 'create', '-volname', f'Dravyn {version}',
                        '-srcfolder', str(stage), '-format', 'UDZO', str(dmg)], check=True)
        subprocess.run(['hdiutil', 'verify', str(dmg)], check=True)
    digest = hashlib.sha256(dmg.read_bytes()).hexdigest()
    (output / 'SHA256SUMS.txt').write_text(f'{digest}  {dmg.name}\n')
    print(f'Packaged: {dmg}\nSHA-256: {digest}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundle', type=Path, required=True, help='App with deployed dependencies')
    parser.add_argument('--binary', type=Path, required=True, help='Executable from the release commit')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--minimum-os', required=True, help='Highest minimum OS of bundled dependencies')
    args = parser.parse_args()
    if not re.fullmatch(r'\d+\.\d+\.\d+', args.version):
        parser.error('Version must have three numeric components')
    package(args.bundle.resolve(strict=True), args.binary.resolve(strict=True),
            args.output.resolve(), args.version, args.minimum_os)
