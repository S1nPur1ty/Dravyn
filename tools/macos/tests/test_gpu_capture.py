# SPDX-License-Identifier: GPL-3.0-or-later

import importlib.util
import os
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location(
    "capture_gpu_frame", Path(__file__).parents[1] / "capture_gpu_frame.py")
capture = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(capture)


class CaptureRequestTests(unittest.TestCase):
    def test_uses_bundle_executable_after_app_rename(self):
        with tempfile.TemporaryDirectory() as directory:
            app = Path(directory) / "Dravyn Diagnostics.app"
            executable = app / "Contents/MacOS/Dravyn"
            executable.parent.mkdir(parents=True)
            executable.touch()
            self.assertEqual(capture.bundle_executable(app, {"CFBundleExecutable": "Dravyn"}),
                             executable.resolve())
            for name in ("", ".", "..", "../Dravyn", "/bin/sh", None):
                with self.assertRaises(ValueError):
                    capture.bundle_executable(app, {"CFBundleExecutable": name})

    def test_selects_only_process_capture_pipe(self):
        listing = "p123\nn/tmp/unrelated\nn/private/tmp/MoltenVKCapturePipe-123\n"
        self.assertEqual(capture.capture_pipe(listing),
                         Path("/private/tmp/MoltenVKCapturePipe-123"))
        for output in ("p123\n", listing + "n/tmp/MoltenVKCapturePipe-456\n"):
            with self.assertRaises(ValueError):
                capture.capture_pipe(output)

    def test_requests_exactly_one_frame(self):
        with tempfile.TemporaryDirectory() as directory:
            pipe = Path(directory) / "capture"
            os.mkfifo(pipe, 0o600)
            descriptor = os.open(pipe, os.O_RDONLY | os.O_NONBLOCK)
            try:
                capture.signal_frame(pipe)
                self.assertEqual(os.read(descriptor, 4096), b"1")
            finally:
                os.close(descriptor)

    def test_rejects_regular_file_and_symlink_without_writing(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "regular"
            target.write_bytes(b"unchanged")
            link = Path(directory) / "link"
            link.symlink_to(target)
            for path in (target, link):
                with self.assertRaises((OSError, ValueError)):
                    capture.signal_frame(path)
                self.assertEqual(target.read_bytes(), b"unchanged")


if __name__ == "__main__":
    unittest.main()
