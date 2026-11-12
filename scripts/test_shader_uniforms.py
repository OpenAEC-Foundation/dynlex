#!/usr/bin/env python3
"""Validate typed shader storage with isolated intrinsics and a library declaration."""
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

F32 = '@intrinsic("type", "float", 32)'
I32 = '@intrinsic("type", "int", 32)'
U32 = '@intrinsic("type", "uint", 32)'


def vector(size, scalar=F32):
    return f'@intrinsic("vector", {size}, {scalar})'


def matrix(rows, columns, scalar=F32):
    return f'@intrinsic("matrix", {rows}, {columns}, {scalar})'


def uniform(kind, name='data', binding=1):
    return f'@intrinsic("shader uniform", "{name}", {binding}, {kind})'


def output(value):
    return f'@intrinsic("shader output", @intrinsic("construct", {vector(4)}, @intrinsic("cast", {value}, {F32}), 0.0, 0.0, 1.0))\n'


def interface(path):
    words = struct.unpack('<' + 'I' * (path.stat().st_size // 4), path.read_bytes())
    types, bindings, blocks, offsets, variables = {}, {}, set(), {}, []
    position = 5
    while position < len(words):
        length, opcode = words[position] >> 16, words[position] & 65535
        assert length > 0
        values = words[position + 1:position + length]
        if opcode in (21, 22, 23, 28, 30, 32):
            types[values[0]] = (opcode, values[1:])
        elif opcode == 71:
            if values[1] == 33: bindings[values[0]] = values[2]
            if values[1] == 2: blocks.add(values[0])
        elif opcode == 72 and values[2] == 35:
            offsets[(values[0], values[1])] = values[3]
        elif opcode == 59 and values[2] == 2:
            variables.append((values[1], values[0]))
        position += length
    assert all(kind != 21 or layout[0] == 32 for kind, layout in types.values()), "32-bit shader inputs acquired a 64-bit indexing requirement"
    assert len(variables) == 1, variables
    identity, pointer = variables[0]
    assert bindings[identity] == 1
    opcode, pointer = types[pointer]
    assert opcode == 32 and pointer[0] == 2
    block = pointer[1]
    assert block in blocks and offsets[(block, 0)] == 0
    opcode, members = types[block]
    assert opcode == 30 and len(members) == 1
    value = members[0]
    opcode, shape = types[value]
    rows, columns = 1, 1
    if opcode == 30:
        fields = shape
        row_opcode, row = types[fields[0]]
        assert row_opcode == 23
        columns = row[1]
        assert 2 <= columns <= 4
        step = 2 if columns < 4 else 1
        assert len(fields) % step == 0
        rows = len(fields) // step
        assert 2 <= rows <= 4
        for index in range(rows):
            assert fields[index * step] == fields[0]
            assert offsets[(value, index * step)] == index * 16
            if step == 2:
                padding_opcode, padding = types[fields[index * step + 1]]
                assert offsets[(value, index * step + 1)] == index * 16 + columns * 4
                if columns == 2:
                    assert padding_opcode == 23 and padding == (row[0], 2)
                else:
                    assert padding_opcode == 22 and padding == (32,)
        opcode, shape = types[row[0]]
    if opcode == 23:
        columns = shape[1]
        opcode, shape = types[shape[0]]
    return opcode, shape, rows, columns


def main():
    compiler = Path(sys.argv[1]).resolve()
    validator = shutil.which('spirv-val')
    assert validator, 'spirv-val is required'
    with tempfile.TemporaryDirectory(prefix='dynlex-shader-uniforms-') as directory:
        folder = Path(directory)
        def compile(source, options=('--emit-spirv', '--shader-stage=vertex')):
            entry, binary = folder / 'main.dl', folder / 'main.spv'
            entry.write_text(source)
            result = subprocess.run([str(compiler), str(entry), *options, '-o', str(binary)],
                                    capture_output=True, text=True, timeout=15)
            return result, binary
        cases = []
        for kind, expected in [(F32, (22, (32,))), (I32, (21, (32, 1))), (U32, (21, (32, 0)))]:
            cases.append((kind, *expected, 1, 1))
            for columns in range(2, 5):
                cases.append((vector(columns, kind), *expected, 1, columns))
        for rows in range(2, 5):
            for columns in range(2, 5):
                cases.append((matrix(rows, columns), 22, (32,), rows, columns))
        for level in ['-O0', '-O2']:
            for kind, opcode, scalar, rows, columns in cases:
                value = uniform(kind)
                if rows > 1:
                    operand = f'@intrinsic("construct", {vector(columns)}, '+', '.join(['1.0']*columns)+')'
                    value = f'@intrinsic("extract element", @intrinsic("multiply", {value}, {operand}), {rows-1})'
                elif columns > 1:
                    value = f'@intrinsic("extract element", {value}, {columns-1})'
                result, binary = compile(output(value), ('--emit-spirv', '--shader-stage=vertex', level))
                assert result.returncode == 0 and not result.stderr, (kind, result.stdout, result.stderr)
                validation = subprocess.run([validator, '--target-env', 'vulkan1.3', str(binary)], capture_output=True, text=True)
                assert validation.returncode == 0, (kind, level, validation.stderr)
                assert interface(binary) == (opcode, scalar, rows, columns), (kind, interface(binary))
        # Distinct blocks may share one physical matrix row type.
        first = uniform(matrix(3, 2), name='first', binding=1)
        second = uniform(matrix(3, 2), name='second', binding=2)
        operand = f'@intrinsic("construct", {vector(2)}, 1.0, 1.0)'
        first = f'@intrinsic("extract element", @intrinsic("multiply", {first}, {operand}), 2)'
        second = f'@intrinsic("extract element", @intrinsic("multiply", {second}, {operand}), 1)'
        for level in ['-O0', '-O2']:
            result, binary = compile(output(f'@intrinsic("add", {first}, {second})'),
                                     ('--emit-spirv', '--shader-stage=vertex', level))
            assert result.returncode == 0 and not result.stderr, result.stderr
            validation = subprocess.run([validator, '--target-env', 'vulkan1.3', str(binary)], capture_output=True, text=True)
            assert validation.returncode == 0, validation.stderr
        source = 'import lib/shader.dl\n' + output('the shader uniform named "camera" at binding 1 as '+F32)
        result, binary = compile(source)
        assert result.returncode == 0 and not result.stderr, result.stderr
        subprocess.run([validator, '--target-env', 'vulkan1.3', str(binary)], check=True, capture_output=True)
        invalid = [
            '@intrinsic("type", "float", 64)', '@intrinsic("type", "bool")', vector(8),
            matrix(1, 3), matrix(5, 4), matrix(3, 3, I32),
            '@intrinsic("add pointer depth", '+F32+')', '42'
        ]
        for kind in invalid:
            result, _ = compile(f'@intrinsic("discard", {uniform(kind)})\n')
            assert result.returncode > 0 and 'Shader uniform requires' in result.stdout + result.stderr, (kind, result.stderr)
        source = f'@intrinsic("discard", {uniform(F32)})\n' + output(f'@intrinsic("extract element", {uniform(vector(4))}, 0)')
        result, _ = compile(source)
        assert result.returncode > 0 and 'uses different types' in result.stdout + result.stderr, result.stderr
    print('Typed shader buffers preserve scalar signedness, vector widths and padded matrix rows at both optimizations.')


if __name__ == '__main__':
    main()
