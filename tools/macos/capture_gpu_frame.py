#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Request one MoltenVK frame capture from an explicitly selected diagnostic app."""

import argparse
from datetime import datetime, timezone
import fcntl
import json
import os
from pathlib import Path
import plistlib
import re
import stat
import subprocess


def capture_pipe(lsof_output):
    paths = {Path(line[1:]) for line in lsof_output.splitlines()
             if line.startswith("n/") and
             Path(line[1:]).name.startswith("MoltenVKCapturePipe-")}
    if len(paths) != 1:
        raise ValueError("Expected exactly one MoltenVK capture pipe for this process")
    return paths.pop()


def signal_frame(pipe):
    descriptor = os.open(pipe, os.O_WRONLY | os.O_NONBLOCK | os.O_NOFOLLOW)
    try:
        info = os.fstat(descriptor)
        if not stat.S_ISFIFO(info.st_mode) or info.st_uid != os.getuid():
            raise ValueError("Capture target is not a FIFO owned by the current user")
        if os.write(descriptor, b"1") != 1:
            raise OSError("Could not request the GPU frame capture")
    finally:
        os.close(descriptor)


def bundle_executable(app, info):
    name = info.get("CFBundleExecutable", "")
    if not isinstance(name, str) or not name or name in (".", "..") or "/" in name:
        raise ValueError("App does not have a valid executable name")
    return (app / "Contents/MacOS" / name).resolve(strict=True)


def request_capture(app, pid):
    app = app.resolve(strict=True)
    with (app / "Contents/Info.plist").open("rb") as stream:
        info = plistlib.load(stream)
    executable = bundle_executable(app, info)
    running = subprocess.check_output(
        ["ps", "-p", str(pid), "-o", "comm="], text=True).strip()
    if Path(running).resolve() != executable:
        raise ValueError("The selected process is not running the selected app")
    profile_name = info.get("EglantineProfile", "")
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", profile_name):
        raise ValueError("App does not have an isolated profile")
    profile = (Path.home() / "Library/Application Support/Eglantine/Profiles" /
               profile_name)
    environment = info.get("LSEnvironment", {})
    if (environment.get("MTL_CAPTURE_ENABLED") != "1" or
            environment.get("MVK_CONFIG_AUTO_GPU_CAPTURE_SCOPE") != "3"):
        raise ValueError("App is not configured for on-demand GPU capture")
    output = Path(environment.get("MVK_CONFIG_AUTO_GPU_CAPTURE_OUTPUT_FILE", ""))
    if output.parent != profile / "dump" or output.suffix != ".gputrace":
        raise ValueError("Capture destination must be inside the isolated dump directory")
    listing = subprocess.check_output(
        ["lsof", "-a", "-p", str(pid), "-Fn"], text=True)
    pipe = capture_pipe(listing)
    output.parent.mkdir(parents=True, exist_ok=True)
    # Hold the lock across the output check and FIFO write; never overwrite a capture.
    with output.with_suffix(".lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        receipt = output.with_suffix(".request.json")
        if output.exists() or receipt.exists():
            raise ValueError("Capture already requested; archive it before requesting another")
        metadata = {"pid": pid, "app": str(app), "profile": str(profile),
                    "capture": str(output), "pipe": str(pipe),
                    "requested_at": datetime.now(timezone.utc).isoformat(),
                    "status": "requested", "frames": 1}
        # Write the receipt first so a failed request cannot be mistaken for a completed trace.
        receipt.write_text(json.dumps(metadata, indent=2) + "\n")
        try:
            trace = profile / "dump/draw-trace"
            trace.mkdir(parents=True, exist_ok=True)
            (trace / "capture.request").touch()
            signal_frame(pipe)
        except Exception:
            metadata["status"] = "request_failed"
            receipt.write_text(json.dumps(metadata, indent=2) + "\n")
            raise
    return metadata


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", required=True, type=Path)
    parser.add_argument("--pid", required=True, type=int)
    args = parser.parse_args()
    if args.pid <= 0:
        parser.error("--pid must be positive")
    try:
        print(json.dumps(request_capture(args.app, args.pid), indent=2))
        print("Capture requested, not yet verified. Leave the app rendering for a few seconds.")
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Capture request failed: {error}\n")
