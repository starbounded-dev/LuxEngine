#!/usr/bin/env python3
"""Compile deferred lighting and reject per-fragment copies of the PCSS sample table."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk-bin', type=Path, help='Vulkan SDK bin directory')
    args = parser.parse_args()
    sdk_bin = args.sdk_bin
    if sdk_bin is None:
        sdk = os.environ.get('VULKAN_SDK')
        sdk_bin = Path(sdk) / 'bin' if sdk else ROOT / 'Core/vendor/VulkanSDK/x86_64/bin'

    def tool(name):
        for candidate in (sdk_bin / name, sdk_bin / (name + '.exe')):
            if candidate.is_file():
                return str(candidate)
        found = shutil.which(name)
        if found:
            return found
        raise RuntimeError(f'{name} not found; pass --sdk-bin with the Vulkan SDK bin directory')

    shaders = ROOT / 'Editor/Resources/Shaders'
    source = (shaders / 'DeferredLighting.glsl').read_text()
    fragment = source[source.rindex('#version'):].replace('#pragma stage : frag', '')
    with tempfile.TemporaryDirectory(prefix='lux-shadow-shader-') as directory:
        directory = Path(directory)
        shader = directory / 'deferred.frag'
        binary = directory / 'deferred.spv'
        shader.write_text(fragment)
        subprocess.run([
            tool('glslc'), '-fshader-stage=frag', '--target-env=vulkan1.2', '-O',
            '-D__GLSL__', '-D__FRAGMENT_STAGE__',
            '-I' + str(shaders / 'Include/GLSL'), '-I' + str(shaders / 'Include/Common'),
            str(shader), '-o', str(binary),
        ], check=True)
        subprocess.run([tool('spirv-val'), '--target-env', 'vulkan1.2', str(binary)], check=True)
        assembly = subprocess.check_output([tool('spirv-dis'), str(binary)], text=True)

    # Follow SPIR-V types rather than relying on generated symbol names. Dynamic
    # indexing used to produce many function-local 64-entry vec2 sample arrays.
    definitions = {}
    for line in assembly.splitlines():
        match = re.match(r'\s*(%\S+) = (Op\S+) (.*)', line)
        if match:
            definitions[match[1]] = (match[2], match[3].split())
    sample_arrays = set()
    for identifier, (opcode, operands) in definitions.items():
        if opcode != 'OpTypeArray':
            continue
        element = definitions.get(operands[0])
        length = definitions.get(operands[1])
        if element and element[0] == 'OpTypeVector' and element[1][1] == '2':
            if length and length[0] == 'OpConstant' and length[1][-1] == '64':
                sample_arrays.add(identifier)
    local_tables = []
    for identifier, (opcode, operands) in definitions.items():
        if opcode != 'OpVariable' or operands[1] != 'Function':
            continue
        pointer = definitions[operands[0]]
        if pointer[0] == 'OpTypePointer' and pointer[1][1] in sample_arrays:
            local_tables.append(identifier)
    if local_tables:
        raise RuntimeError(f'PCSS regressed: {len(local_tables)} per-fragment sample-table copies')
    print('PASS: deferred lighting validates and has no function-local 64-sample tables')


if __name__ == '__main__':
    main()
