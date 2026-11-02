#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Seed a new profile from validated cache snapshots without modifying the sources."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile


def compatible_header(path, kind, version, driver_identity):
    with path.open('rb') as stream:
        header = stream.read(44)
    magic = b'yuzucach' if kind == 'vulkan.bin' else b'yuzuvkch'
    if len(header) < 12 or header[:8] != magic:
        return False
    if struct.unpack_from('<I', header, 8)[0] != version:
        return False
    return kind == 'vulkan.bin' or (len(header) == 44 and header[12:44] == driver_identity)


def snapshot(source, destination):
    for _ in range(3):
        before = source.stat()
        if source.is_symlink():
            raise ValueError('Refusing a symlinked cache')
        shutil.copyfile(source, destination)
        after = source.stat()
        fields = lambda s: (s.st_dev, s.st_ino, s.st_size, s.st_mtime_ns)
        if fields(before) == fields(after) and destination.stat().st_size == before.st_size:
            return
    raise ValueError('Cache is changing; leave the running instance alone and skip it')


def seed(sources, destination, version, driver_identity, validate):
    destination.mkdir(parents=True, exist_ok=True)
    candidates = {}
    for source in sources:
        for path in source.glob('*/vulkan*.bin'):
            title = path.parent.name
            if len(title) != 16 or any(c not in '0123456789abcdefABCDEF' for c in title):
                continue
            if path.name not in ('vulkan.bin', 'vulkan_pipelines.bin') or path.is_symlink():
                continue
            candidates.setdefault(Path(title.lower()) / path.name, []).append(path)
    report = {'copied': [], 'skipped': []}
    for relative, paths in sorted(candidates.items()):
        target = destination / relative
        if target.exists():
            report['skipped'].append({'target': str(target), 'reason': 'Already exists; never overwrite'})
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        for source in sorted(paths, key=lambda p: p.stat().st_size, reverse=True):
            fd, name = tempfile.mkstemp(prefix='.cache-import-', dir=target.parent)
            os.close(fd)
            temporary = Path(name)
            try:
                snapshot(source, temporary)
                if not compatible_header(temporary, target.name, version, driver_identity):
                    raise ValueError('Cache schema or GPU/driver identity differs')
                validate(temporary, target.name)
                digest = hashlib.sha256(temporary.read_bytes()).hexdigest()
                # Publish without replacing a cache created concurrently by the destination app.
                os.link(temporary, target)
                report['copied'].append({'source': str(source), 'target': str(target),
                                         'bytes': target.stat().st_size, 'sha256': digest})
                break
            except (OSError, ValueError, subprocess.SubprocessError) as error:
                report['skipped'].append({'source': str(source), 'reason': str(error)})
            finally:
                temporary.unlink(missing_ok=True)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, action='append', required=True)
    parser.add_argument('--destination', type=Path, required=True)
    parser.add_argument('--version', type=int, required=True)
    parser.add_argument('--driver-identity', required=True,
                        help='32-byte Vulkan cache identity from the packaged-device smoke test, hex')
    parser.add_argument('--guest-validator', type=Path, required=True)
    parser.add_argument('--driver-validator', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    identity = bytes.fromhex(args.driver_identity)
    if len(identity) != 32:
        parser.error('driver identity must be 32 bytes')

    def validate(path, kind):
        if kind == 'vulkan.bin':
            env = dict(os.environ, EDEN_TEST_PIPELINE_CACHE_COPY=str(path))
            subprocess.run([str(args.guest_validator), '[pipeline-cache-audit]'], env=env,
                           check=True, timeout=60)
        else:
            subprocess.run([str(args.driver_validator), '--game-cache', str(path)],
                           check=True, timeout=60)
    report = seed(args.source, args.destination, args.version, identity, validate)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
