#!/usr/bin/env python3
"""Check explicit cross-stage locations, typed interpolation and face orientation."""
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

F32 = '@intrinsic("type", "float", 32)'
I32 = '@intrinsic("type", "int", 32)'
U32 = '@intrinsic("type", "uint", 32)'
V4 = f'@intrinsic("vector", 4, {F32})'
OUTPUT = f'@intrinsic("shader output", @intrinsic("construct", {V4}, 0.0, 0.0, 0.0, 1.0))\n'


def read(name, location, mode, kind):
    return f'@intrinsic("shader interpolant input", "{name}", {location}, "{mode}", {kind})'


def write(name, location, mode, value):
    return f'@intrinsic("shader interpolant output", "{name}", {location}, "{mode}", {value})\n'


def discard(value):
    return f'@intrinsic("discard", {value})\n'


def interface(path):
    words = struct.unpack('<' + 'I' * (path.stat().st_size // 4), path.read_bytes())
    names, decorations, types, variables = {}, {}, {}, {}
    index = 5
    while index < len(words):
        length, opcode = words[index] >> 16, words[index] & 65535
        assert length, 'Incomplete SPIR-V instruction'
        values = words[index + 1:index + length]
        if opcode == 5:
            names[values[0]] = struct.pack('<' + 'I' * len(values[1:]), *values[1:]).split(b'\0', 1)[0].decode()
        elif opcode == 71:
            decorations.setdefault(values[0], {})[values[1]] = values[2:]
        elif opcode in (20, 21, 22, 23, 32):
            types[values[0]] = (opcode, values[1:])
        elif opcode == 59:
            variables[values[1]] = (values[0], values[2])
        index += length
    result = {}
    for identity, (pointer, storage) in variables.items():
        name = names.get(identity, '')
        if not name.startswith('dynlex_interpolant_') and name != 'gl_FrontFacing':
            continue
        opcode, shape = types[types[pointer][1][1]]
        count = 1
        if opcode == 23:
            count = shape[1]
            opcode, shape = types[shape[0]]
        result[name] = (storage, opcode, shape, count, decorations[identity])
    return result


def main():
    compiler = Path(sys.argv[1]).resolve()
    validator = shutil.which('spirv-val')
    assert validator, 'spirv-val is required'
    with tempfile.TemporaryDirectory(prefix='dynlex-interpolants-') as temporary:
        folder = Path(temporary)

        def compile(name, source, stage='vertex', optimization='-O0', arguments=None):
            entry, output = folder / (name + '.dl'), folder / (name + '.spv')
            entry.write_text(source)
            flags = ['--emit-spirv', '--shader-stage=' + stage] if arguments is None else arguments
            result = subprocess.run([str(compiler), str(entry), *flags, optimization, '-o', str(output)],
                                    text=True, capture_output=True, timeout=15)
            return result, output

        for optimization in ('-O0', '-O2'):
            # The fragment consumes a subset, in a different declaration order.
            declarations = [('z color', 7, 'perspective', V4, f'@intrinsic("construct", {V4}, 0.1, 0.2, 0.3, 1.0)'),
                            ('unused', 0, 'perspective', F32, '@intrinsic("cast", 0.5, ' + F32 + ')'),
                            ('material', 3, 'flat', U32, '@intrinsic("cast", 4294967295, ' + U32 + ')'),
                            ('signed', 9, 'flat', f'@intrinsic("vector", 2, {I32})',
                             f'@intrinsic("construct", @intrinsic("vector", 2, {I32}), @intrinsic("negate", 9), 12)')]
            for stage in ('vertex', 'fragment'):
                chosen = declarations if stage == 'vertex' else list(reversed(declarations[::3] + declarations[2:3]))
                source = ''.join(write(name, location, mode, value) if stage == 'vertex' else
                                 discard(read(name, location, mode, kind))
                                 for name, location, mode, kind, value in chosen) + OUTPUT
                result, path = compile(stage + optimization, source, stage, optimization)
                assert result.returncode == 0, result.stdout + result.stderr
                subprocess.run([validator, '--target-env', 'vulkan1.3', str(path)], check=True, capture_output=True, text=True)
                values = interface(path)
                for name, location, mode, kind, _ in chosen:
                    entry = values['dynlex_interpolant_' + name.encode().hex()]
                    assert entry[0] == (3 if stage == 'vertex' else 1), entry
                    assert entry[4][30] == (location,), entry
                    assert (14 in entry[4]) == (mode == 'flat'), entry
                    expected = (21, (32, 0), 1) if kind == U32 else (21, (32, 1), 2) if name == 'signed' else (22, (32,), 4 if kind == V4 else 1)
                    assert entry[1:4] == expected, (name, entry)
            result, path = compile('face' + optimization, discard('@intrinsic("shader input", "FrontFacing")') + OUTPUT,
                                   'fragment', optimization)
            assert result.returncode == 0, result.stdout + result.stderr
            subprocess.run([validator, '--target-env', 'vulkan1.3', str(path)], check=True, capture_output=True, text=True)
            assert interface(path)['gl_FrontFacing'] == (1, 20, (), 1, {11: (17,)})

            # Dynamic outputs exercise signless LLVM stores, not just constants.
            for scalar, opcode, shape in [(F32, 22, (32,)), (I32, 21, (32, 1)), (U32, 21, (32, 0))]:
                for count in range(1, 5):
                    kind = scalar if count == 1 else f'@intrinsic("vector", {count}, {scalar})'
                    value = f'@intrinsic("shader vertex input", "data", 0, {kind})'
                    for stage in ('vertex', 'fragment'):
                        source = write('data', 2, 'flat', value) if stage == 'vertex' else discard(read('data', 2, 'flat', kind))
                        result, path = compile(f'dynamic-{opcode}-{shape}-{count}-{stage}', source + OUTPUT, stage, optimization)
                        assert result.returncode == 0, result.stdout + result.stderr
                        subprocess.run([validator, '--target-env', 'vulkan1.3', str(path)], check=True, capture_output=True, text=True)
                        entry = interface(path)['dynlex_interpolant_64617461']
                        assert entry[1:4] == (opcode, shape, count), entry

        scalar = '@intrinsic("cast", 1, ' + I32 + ')'
        invalid = [
            ('location', write('x', '@intrinsic("negate", 1)', 'flat', scalar), 'non-negative 32-bit compile-time integer'),
            ('wide-location', write('x', 4294967296, 'flat', scalar), 'non-negative 32-bit compile-time integer'),
            ('fractional-location', write('x', 1.5, 'flat', scalar), 'non-negative 32-bit compile-time integer'),
            ('mode', write('x', 0, 'other', scalar), 'perspective or flat'),
            ('integer-smooth', write('x', 0, 'perspective', scalar), 'Integer shader interpolants require flat interpolation'),
            ('boolean', write('x', 0, 'flat', '@intrinsic("equal", 1, 1)'), '32-bit numeric scalar'),
            ('float64', write('x', 0, 'flat', '@intrinsic("cast", 0.5, @intrinsic("type", "float", 64))'), '32-bit numeric scalar'),
            ('duplicate', write('x', 3, 'flat', scalar) + write('y', 3, 'flat', scalar), 'assigned to both'),
            ('location-conflict', write('x', 3, 'flat', scalar) + write('x', 4, 'flat', scalar), 'different locations, types, or interpolation'),
            ('type-conflict', write('x', 3, 'flat', scalar) + write('x', 3, 'flat', '@intrinsic("cast", 1, ' + U32 + ')'), 'different locations, types, or interpolation'),
            ('mode-conflict', write('x', 3, 'flat', '@intrinsic("cast", 1, ' + F32 + ')') + write('x', 3, 'perspective', '@intrinsic("cast", 1, ' + F32 + ')'), 'different locations, types, or interpolation'),
        ]
        for name, source, message in invalid:
            result, _ = compile(name, source + OUTPUT)
            assert result.returncode > 0 and message in result.stdout + result.stderr, (name, result.stdout, result.stderr)
        for stage in ('vertex', 'compute'):
            result, _ = compile('face-' + stage, discard('@intrinsic("shader input", "FrontFacing")'), stage)
            assert result.returncode > 0 and 'unavailable for this shader stage' in result.stdout + result.stderr, result.stderr
        for arguments in ([], ['--emit-wasm']):
            result, _ = compile('host', discard(read('x', 3, 'flat', U32)), arguments=arguments)
            assert result.returncode > 0 and 'only available when emitting SPIR-V' in result.stdout + result.stderr, result.stderr
    print('Explicit subset interfaces, typed perspective/flat interpolants and Boolean front-face input pass at O0/O2.')


if __name__ == '__main__':
    main()
