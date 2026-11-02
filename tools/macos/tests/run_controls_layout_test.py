#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check popup geometry against the built frontend resources without loading a game."""
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
commands = json.loads((build / 'compile_commands.json').read_text())
command = next(c for c in commands if c['file'].endswith('/yuzu/loading_screen.cpp'))
flags = shlex.split(command['command'])
i = flags.index('-o')
del flags[i:i + 2]
flags.remove('-c')
flags.remove(command['file'])
test = output / 'controls-layout-test'
flags += ['-I/opt/homebrew/lib/QtTest.framework/Headers',
          str(Path(__file__).with_name('controls_layout.cpp').resolve()),
          str(build / 'src/yuzu/CMakeFiles/yuzu.dir/yuzu_autogen/EWIEGA46WW/qrc_yuzu.cpp.o'),
          '-framework', 'QtTest', '-framework', 'QtWidgets', '-framework', 'QtGui',
          '-framework', 'QtCore', '-Wl,-rpath,/opt/homebrew/lib', '-o', str(test)]
subprocess.run(flags, cwd=command['directory'], check=True)
env = os.environ.copy()
env['QT_QPA_PLATFORM'] = 'offscreen'
subprocess.run([str(test)], env=env, check=True)
