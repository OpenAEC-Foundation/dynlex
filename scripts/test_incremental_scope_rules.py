#!/usr/bin/env python3
"""Keep static scope-rule references active when unused runtime bodies are skipped."""
from pathlib import Path
import json
import subprocess
import sys
import tempfile


def check_scope_rule_after_local_edit(root, build):
    entry = root / 'scope_rule_local_edit.dl'
    target = root / 'scope_rule_local_edit.out'
    source = '''integer means: @intrinsic("type", "int", 32)
{integer:bits} bit means: @intrinsic("type", "any", bits)
numbers are properties means: @intrinsic("number rule", properties)
function a matrix:
    execute:
        in this function, by default:
            numbers are 32 bit
        @intrinsic("return", 1.0)
function advance {@intrinsic("type", "float", 64):seconds}:
    execute:
BODY
@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%f\\n", advance @intrinsic("cast", @intrinsic("call", "libc", "atoi", integer, "2"), @intrinsic("type", "float", 64))))
'''
    bodies = [
        '        @intrinsic("return", @intrinsic("divide", @intrinsic("multiply", seconds, 6.28), 0.85))',
        '        @intrinsic("store", {phase}, @intrinsic("multiply", seconds, 6.28))\n'
        '        @intrinsic("return", @intrinsic("divide", phase, 0.85))',
    ]
    for body in [*bodies, bodies[0]]:
        entry.write_text(source.replace('BODY', body))
        edited = build(entry=entry, target=target)
        clean = build(False, entry=entry, target=target)
        assert edited[1:] == clean[1:], 'A body edit changed a static numeric scope rule'
        assert edited[1:] == ('', b'14.776471\n')
        assert build(entry=entry, target=target)[1:] == clean[1:]


def main():
    project = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix='dynlex-incremental-rules-') as temporary:
        root = Path(temporary)

        def build(incremental=True, *, entry, target):
            output = target if incremental else target.with_suffix('.clean.out')
            options = ['--cache-dir', str(root / 'cache'), '--incremental-stats'] if incremental else ['--no-incremental']
            result = subprocess.run([str(project / 'build/dynlex'), str(entry), '-O0', *options, '-o', str(output)],
                                    cwd=root, text=True, capture_output=True, timeout=30)
            assert result.returncode == 0, result.stderr
            lines = result.stderr.splitlines(keepends=True)
            statistics = [line for line in lines if line.startswith('incremental: ')]
            stats = json.loads(statistics[0].removeprefix('incremental: ')) if statistics else None
            diagnostics = ''.join(line for line in lines if not line.startswith('incremental: '))
            runtime = subprocess.run([str(output)], cwd=root, check=True, capture_output=True, timeout=5)
            return stats, diagnostics, runtime.stdout

        check_scope_rule_after_local_edit(root, build)
    print('Incremental body edits retain static numeric scope-rule references.')


if __name__ == '__main__':
    main()
