# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('seed', Path(__file__).parents[1] / 'seed_shader_cache.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class CacheSeedTests(unittest.TestCase):
    def test_reuse_is_a_private_copy_and_never_overwrites(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); source = root / 'old'; target = root / 'new'
            game = source / '0400c3f00006e000'; game.mkdir(parents=True)
            cache = game / 'vulkan.bin'; data = b'yuzucach' + struct.pack('<I', 18) + b'payload'
            cache.write_bytes(data)
            report = module.seed([source], target, 18, bytes(32), lambda p, k: None)
            copied = target / game.name / cache.name
            self.assertEqual(len(report['copied']), 1)
            self.assertEqual(copied.read_bytes(), data)
            self.assertNotEqual(cache.stat().st_ino, copied.stat().st_ino)
            cache.write_bytes(data + b'changed')
            module.seed([source], target, 18, bytes(32), lambda p, k: None)
            self.assertEqual(copied.read_bytes(), data)

    def test_incompatible_and_corrupt_caches_are_not_published(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); source = root / 'old'; game = source / '0400c3f00006e000'
            game.mkdir(parents=True)
            guest = game / 'vulkan.bin'
            guest.write_bytes(b'yuzucach' + struct.pack('<I', 17) + b'payload')
            driver = game / 'vulkan_pipelines.bin'
            driver.write_bytes(b'yuzuvkch' + struct.pack('<I', 18) + bytes([1]) * 32)
            report = module.seed([source], root / 'new', 18, bytes(32), lambda p, k: None)
            self.assertFalse(report['copied'])
            self.assertEqual(len(report['skipped']), 2)
            guest.write_bytes(b'yuzucach' + struct.pack('<I', 18) + b'bad record')
            def reject(path, kind):
                raise ValueError('Invalid cache record')
            report = module.seed([source], root / 'new', 18, bytes(32), reject)
            self.assertFalse(report['copied'])
            self.assertFalse(list((root / 'new').glob('*/.cache-import-*')))

if __name__ == '__main__':
    unittest.main()
