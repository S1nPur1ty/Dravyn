#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the built loading-screen widget offscreen, without loading an emulator."""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('build_directory', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
build = args.build_directory.resolve()
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=True)
root = Path(__file__).resolve().parents[3]
commands = json.loads((build / 'compile_commands.json').read_text())
command = next(c for c in commands if c['file'].endswith('/yuzu/loading_screen.cpp'))
flags = shlex.split(command['command'])
index = flags.index('-o')
del flags[index:index + 2]
flags.remove('-c')
flags.remove(command['file'])
test = output / 'loading-screen-test'
flags += ['-I' + str(build / 'src/yuzu/yuzu_autogen/EWIEGA46WW'),
    str(Path(__file__).with_name('loading_screen.cpp')),
    str(build / 'src/yuzu/CMakeFiles/yuzu.dir/loading_screen.cpp.o'),
    '-framework', 'QtWidgets', '-framework', 'QtGui', '-framework', 'QtCore',
    '-Wl,-rpath,/opt/homebrew/lib', '-o', str(test)]
subprocess.run(flags, cwd=command['directory'], check=True)
env = os.environ.copy()
env['QT_QPA_PLATFORM'] = 'offscreen'
subprocess.run([str(test)], env=env, check=True)
