#!/usr/bin/env python3
"""Compile the standard vector vocabulary for both shader stages and optimizations."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    root = Path(__file__).resolve().parents[1]
    compiler = Path(sys.argv[1]).resolve()
    validator = shutil.which('spirv-val')
    assert validator, 'spirv-val is required'
    entry = root / 'tests/runtime/graphics_shaders/vector.dl'
    with tempfile.TemporaryDirectory(prefix='dynlex-vector-shaders-') as directory:
        for stage in ['vertex', 'fragment']:
            for level in ['-O0', '-O2']:
                binary = Path(directory) / 'vector.spv'
                result = subprocess.run([str(compiler), str(entry), '--emit-spirv',
                                         '--shader-stage=' + stage, level, '-o', str(binary)],
                                        cwd=root, capture_output=True, text=True, timeout=15)
                assert result.returncode == 0 and not result.stderr, (stage, level, result.stdout, result.stderr)
                subprocess.run([validator, '--target-env', 'vulkan1.3', str(binary)],
                               check=True, capture_output=True, text=True, timeout=15)
    print('Vector arithmetic, coordinate reads and independent coordinate updates compile for both shader stages.')


if __name__ == '__main__':
    main()
