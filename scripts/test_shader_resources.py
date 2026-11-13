#!/usr/bin/env python3
"""Verify native shader resource lowering and reject invalid resource contracts."""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


def main():
    root = Path(__file__).resolve().parents[1]
    compiler = Path(sys.argv[1]).resolve()
    validator, disassembler = shutil.which('spirv-val'), shutil.which('spirv-dis')
    assert validator and disassembler, 'spirv-tools is required'
    sources = [(root / 'tests/runtime/graphics_shaders' / name, bindings)
               for name, bindings in [('resources.dl', 7), ('resource_vocabulary.dl', 6)]]
    with tempfile.TemporaryDirectory(prefix='dynlex-shader-resources-') as directory:
        work = Path(directory)
        binary = work / 'resources.spv'
        for source, bindings in sources:
            for stage in ['vertex', 'fragment']:
                for level in ['-O0', '-O2']:
                    result = subprocess.run([str(compiler), str(source), '--emit-spirv',
                                             '--shader-stage=' + stage, level, '-o', str(binary)],
                                            cwd=root, capture_output=True, text=True, timeout=30)
                    assert result.returncode == 0 and not result.stderr, (stage, level, result.stdout, result.stderr)
                    validation = subprocess.run([validator, '--target-env', 'vulkan1.3', str(binary)],
                                                capture_output=True, text=True, timeout=15)
                    assert validation.returncode == 0, (source.name, stage, level, validation.stderr)
                    assembly = subprocess.check_output([disassembler, str(binary)], text=True)
                    assert 'OpCapability Int8' not in assembly, 'Binding-name metadata acquired runtime byte storage'
                    assert 'dynlex_sample_' not in assembly, 'An internal sample call survived backend lowering'
                    assert 'OpImageSampleExplicitLod' in assembly, assembly
                    assert 'OpImageSampleDrefExplicitLod' in assembly, assembly
                    assert re.search(r'OpTypeImage %\w+ Cube 0 0 0 1 Unknown', assembly), assembly
                    assert re.search(r'OpTypeImage %\w+ 2D 1 0 0 1 Unknown', assembly), assembly
                    assert set(map(int, re.findall(r'OpDecorate %\w+ Binding (\d+)', assembly))) == set(range(bindings)), assembly
        cases = [
            ('empty name', '@intrinsic("shader image", "", 0, "2d", false)', 'nonempty string'),
            ('binding range', '@intrinsic("shader image", "x", @intrinsic("negate", 1), "2d", false)', 'non-negative 32-bit'),
            ('dimension', '@intrinsic("shader image", "x", 1, "volume", false)', '2d or cube'),
            ('depth cube', '@intrinsic("shader image", "x", 1, "cube", true)', '2d image'),
            ('depth flag', '@intrinsic("shader image", "x", 1, "2d", 1)', 'boolean'),
            ('sampler flag', '@intrinsic("shader sampler", "s", 2, 1)', 'boolean'),
        ]
        color = '@intrinsic("shader image", "paint", 1, "2d", false)'
        cube = '@intrinsic("shader image", "sky", 1, "cube", false)'
        depth = '@intrinsic("shader image", "shadow", 1, "2d", true)'
        sampler = '@intrinsic("shader sampler", "smooth", 2, false)'
        comparison = '@intrinsic("shader sampler", "compare", 2, true)'
        uv = '@intrinsic("construct", @intrinsic("vector", 2), 0.0, 0.0)'
        def sample(image=color, filtering=sampler, coordinates=uv, scalar='0.0', compare=False):
            operation = 'shader sample compare' if compare else 'shader sample level'
            return f'@intrinsic("{operation}", {image}, {filtering}, {coordinates}, {scalar})'
        cases += [
            ('not an image', sample(image='1'), 'requires a shader image'),
            ('not a sampler', sample(filtering=color), 'requires a shader sampler'),
            ('depth color sample', sample(image=depth), 'depth comparison requires'),
            ('comparison color sample', sample(filtering=comparison), 'depth comparison requires'),
            ('color depth sample', sample(filtering=comparison, compare=True), 'depth comparison requires'),
            ('ordinary depth sampler', sample(image=depth, compare=True), 'depth comparison requires'),
            ('cube coordinates', sample(image=cube), 'three for cube'),
            ('scalar coordinates', sample(coordinates='0.0'), 'two components'),
            ('integer level', sample(scalar='0'), 'float32 scalar'),
            ('double level', sample(scalar='@intrinsic("cast", 0.0, @intrinsic("type", "float", 64))'), 'float32 scalar'),
        ]
        for label, expression, message in cases:
            entry = work / 'invalid.dl'
            entry.write_text('false means: @intrinsic("equal", 0, 1)\n'
                             'true means: @intrinsic("equal", 0, 0)\n'
                             '@intrinsic("discard", ' + expression + ')\n')
            result = subprocess.run([str(compiler), str(entry), '--emit-spirv', '-o', str(binary)],
                                    capture_output=True, text=True, timeout=15)
            assert result.returncode > 0 and message in result.stdout + result.stderr, (label, result)
        for label, image, filtering, extra, message in [
            ('image/sampler collision', color, '@intrinsic("shader sampler", "s", 1, false)', '', 'conflicts between'),
            ('same name different kind', color, '@intrinsic("shader sampler", "paint", 2, false)', '', 'different bindings'),
            ('uniform allocated first', color, sampler, '@intrinsic("discard", @intrinsic("shader uniform", "time", 1, @intrinsic("type", "float", 32)))\n', 'conflicts between'),
            ('uniform allocated last', color, sampler, '', 'conflicts between'),
        ]:
            suffix = '@intrinsic("discard", @intrinsic("shader uniform", "time", 1, @intrinsic("type", "float", 32)))\n' if label == 'uniform allocated last' else ''
            entry.write_text('false means: @intrinsic("equal", 0, 1)\n' + extra +
                             '@intrinsic("shader output", ' + sample(image, filtering) + ')\n' + suffix)
            result = subprocess.run([str(compiler), str(entry), '--emit-spirv', '-o', str(binary)],
                                    capture_output=True, text=True, timeout=15)
            assert result.returncode > 0 and message in result.stdout + result.stderr, (label, result)
    print('Shader 2D/cube textures, shared samplers and depth comparisons validate for both stages at O0/O2.')


if __name__ == '__main__':
    main()
