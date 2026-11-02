#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Create valid/invalid headless pipelines and verify Metal errors are recoverable."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('driver_source', type=Path)
parser.add_argument('library_directory', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
source = args.driver_source.resolve()
library = args.library_directory.resolve()
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=True)
shaders = {
    'vert': '#version 450\nvoid main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n',
    'frag': '#version 450\nlayout(location=0) out uvec4 color;\n'
            'void main() { color = uvec4(1, 2, 3, 4); }\n',
}
spirv = []
for stage, code in shaders.items():
    path = output / ('validation.' + stage)
    path.write_text(code)
    binary = path.with_suffix(path.suffix + '.spv')
    subprocess.run(['glslangValidator', '-V', str(path), '-o', str(binary)], check=True)
    spirv.append(str(binary))
executable = output / 'pipeline-validation'
subprocess.run(['clang++', '-std=c++17',
    '-I' + str(source / 'External/Vulkan-Headers/include'),
    str(Path(__file__).with_name('pipeline_validation.cpp')),
    '-L' + str(library), '-lMoltenVK', '-Wl,-rpath,' + str(library),
    '-o', str(executable)], check=True)
subprocess.run([str(executable), *spirv], check=True, timeout=60)
