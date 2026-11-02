#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Link the controller canvas check against the built frontend, without launching a game."""
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
command = next(c for c in commands if c['file'].endswith('/yuzu/game/game_list.cpp'))
flags = shlex.split(command['command'])
i = flags.index('-o')
flags[i + 1] = str(output / 'controller-canvas.o')
flags[flags.index(command['file'])] = str(Path(__file__).with_name('controller_canvas.cpp').resolve())
flags += ['-iframework', '/opt/homebrew/lib', '-I/opt/homebrew/lib/QtTest.framework/Headers']
subprocess.run(flags, cwd=command['directory'], check=True)
commands = subprocess.check_output(['ninja', '-t', 'commands', 'yuzu'], cwd=build, text=True)
link = shlex.split(commands.strip().splitlines()[-1])
link = link[link.index('&&') + 1:]
link = link[:link.index('&&')]
link[link.index('src/yuzu/CMakeFiles/yuzu.dir/main.cpp.o')] = str(output / 'controller-canvas.o')
link[link.index('-o') + 1] = str(output / 'controller-canvas-test')
link += ['/opt/homebrew/lib/QtTest.framework/Versions/A/QtTest', '-Wl,-rpath,/opt/homebrew/lib']
subprocess.run(link, cwd=build, check=True)
env = os.environ.copy()
env['QT_QPA_PLATFORM'] = 'offscreen'
subprocess.run([str(output / 'controller-canvas-test')], env=env, check=True)
