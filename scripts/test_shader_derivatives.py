#!/usr/bin/env python3
"""Verify fragment derivatives through import-free scalar/vector shader programs."""
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


F32 = '@intrinsic("type", "float", 32)'


def derivative_source(axis, lanes):
    kind = F32 if lanes == 1 else f'@intrinsic("vector", {lanes}, {F32})'
    value = f'@intrinsic("shader uniform", "value", 0, {kind})'
    derivative = f'@intrinsic("shader derivative", "{axis}", {value})'
    if lanes == 4:
        return f'@intrinsic("shader output", {derivative})\n'
    components = ['result'] if lanes == 1 else [f'@intrinsic("extract element", result, {index})' for index in range(lanes)]
    components += ['0.0'] * (3 - lanes) + ['1.0']
    return f'@intrinsic("store", {{result}}, {derivative})\n@intrinsic("shader output", @intrinsic("construct", @intrinsic("vector", 4), {", ".join(components)}))\n'


def instructions(binary):
    words = struct.unpack('<' + 'I' * (len(binary) // 4), binary)
    cursor = 5
    while cursor < len(words):
        count, opcode = words[cursor] >> 16, words[cursor] & 65535
        assert count > 0
        yield opcode, words[cursor + 1:cursor + count]
        cursor += count


def main():
    compiler = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix='dynlex-shader-derivatives-') as directory:
        source, binary = Path(directory) / 'main.dl', Path(directory) / 'main.spv'
        for axis, opcode in [('x', 207), ('y', 208)]:
            for lanes in range(1, 5):
                source.write_text(derivative_source(axis, lanes))
                for level in ['-O0', '-O2']:
                    result = subprocess.run([str(compiler), str(source), '--emit-spirv', '--shader-stage=fragment', level, '-o', str(binary)], capture_output=True, text=True, timeout=15)
                    assert result.returncode == 0 and not result.stderr, (axis, lanes, level, result.stderr)
                    subprocess.run(['spirv-val', '--target-env', 'vulkan1.3', str(binary)], check=True, capture_output=True)
                    assert any(kind == opcode for kind, _ in instructions(binary.read_bytes())), (axis, lanes, level)
        source.write_text((Path(__file__).resolve().parents[1] / 'tests/runtime/graphics_shaders/derivatives.dl').read_text())
        subprocess.run([str(compiler), str(source), '--emit-spirv', '--shader-stage=fragment', '-o', str(binary)], check=True, capture_output=True)
        subprocess.run(['spirv-val', '--target-env', 'vulkan1.3', str(binary)], check=True, capture_output=True)
        opcodes = [kind for kind, _ in instructions(binary.read_bytes())]
        assert 207 in opcodes and 208 in opcodes
        value = f'@intrinsic("cast", 1.0, {F32})'
        operation = f'@intrinsic("discard", @intrinsic("shader derivative", "x", {value}))\n'
        for options, message in [([], 'only available when emitting SPIR-V'), (['--emit-wasm'], 'only available when emitting SPIR-V'),
                                 (['--emit-spirv', '--shader-stage=vertex'], 'only available in fragment shaders'),
                                 (['--emit-spirv', '--shader-stage=compute'], 'only available in fragment shaders')]:
            source.write_text(operation)
            result = subprocess.run([str(compiler), str(source), *options, '-o', str(binary)], capture_output=True, text=True, timeout=15)
            assert result.returncode != 0 and message in result.stderr, (options, result.stderr)
        for axis, operand, message in [
            ('"z"', value, 'axis must be'), ('0', value, 'axis must be'),
            ('"x"', '1', 'float32 scalar or vector'),
            ('"x"', '@intrinsic("equal", 1, 1)', 'float32 scalar or vector'),
            ('"x"', '"text"', 'float32 scalar or vector'),
            ('"x"', '@intrinsic("construct", @intrinsic("vector", 2, @intrinsic("type", "int", 32)), 1, 2)', 'float32 scalar or vector'),
            ('@intrinsic("shader uniform", "axis", 1, @intrinsic("type", "int", 32))', value, 'compile-time known'),
        ]:
            source.write_text(f'@intrinsic("discard", @intrinsic("shader derivative", {axis}, {operand}))\n')
            result = subprocess.run([str(compiler), str(source), '--emit-spirv', '--shader-stage=fragment', '-o', str(binary)], capture_output=True, text=True, timeout=15)
            assert result.returncode != 0 and message in result.stderr, (axis, operand, result.stderr)
    print('Fragment derivatives preserve scalar/vector shapes and reject unavailable stages, targets, axes and operand types.')


if __name__ == '__main__':
    main()
