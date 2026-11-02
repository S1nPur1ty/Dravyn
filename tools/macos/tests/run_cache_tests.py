#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Headless regression tests for the patched MoltenVK shader cache; no game launch."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('driver_source', type=Path)
parser.add_argument('library_directory', type=Path)
parser.add_argument('output', type=Path)
parser.add_argument('--cache-copy', type=Path, help='Optional read-only copy of Eglantine vulkan_pipelines.bin')
args = parser.parse_args()
source = args.driver_source.resolve()
library = args.library_directory.resolve()
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=True)
test = output / 'cache-test'
subprocess.run(['clang++', '-std=c++17',
    '-I' + str(source / 'External/Vulkan-Headers/include'),
    '-I' + str(source / 'MoltenVK/MoltenVK/API'),
    str(Path(__file__).with_name('cache_loading.cpp')), '-L' + str(library), '-lMoltenVK',
    '-Wl,-rpath,' + str(library), '-o', str(test)], check=True)
shaders = []
for i in range(8):
    shader = output / f'cache-{i}.comp'
    shader.write_text('#version 450\nlayout(local_size_x=1) in;\n'
        'layout(set=0,binding=0) buffer Output { uint value; } out_buffer;\n'
        'void main() { out_buffer.value = gl_GlobalInvocationID.x + ' + str(i + 42) + 'u; }\n')
    spirv = shader.with_suffix('.spv')
    subprocess.run(['glslangValidator', '-V', str(shader), '-o', str(spirv)], check=True)
    shaders.append(str(spirv))
subprocess.run([str(test), *shaders], check=True)
if args.cache_copy:
    subprocess.run([str(test), '--game-cache', str(args.cache_copy.resolve())], check=True)
