#!/usr/bin/env python3

import os
from pathlib import Path
import subprocess
import sys
import tempfile


def shared_member_program(class_count: int) -> str:
    source = '''function integer:
    replacement:
        @intrinsic("type", "int")

function set variable to value:
    replacement:
        @intrinsic("store", variable, value)

function print value:
    replacement:
        @intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", value))

function left plus right:
    after: default
    replacement:
        @intrinsic("add", left, right)

function left minus right:
    after: default
    replacement:
        @intrinsic("subtract", left, right)

function left times right:
    before: default
    replacement:
        @intrinsic("multiply", left, right)

'''
    for index in range(class_count):
        source += f'''class record{index}:
    members:
        x as integer
        y as integer
        z as integer

'''
    return source + f'''set first to @intrinsic("construct", record0, 1, 2, 3)
set items to @intrinsic("construct", record{class_count - 1}, 4, 5, 6)
print first's x plus items' y times items' z
print the x of first
print items' z
print 10 minus 2 plus 3
'''


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {Path(sys.argv[0]).name} <compiler>", file=sys.stderr)
        return 2
    compiler = Path(sys.argv[1]).resolve()
    timeout = 90 if os.name == "nt" else 30
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        source = root / "shared_members.dl"
        executable = root / ("shared_members.exe" if os.name == "nt" else "shared_members.out")
        # These exact-type overloads share nine member-access syntax families.
        # Their precedence must not be rebuilt for every class and overload pair.
        source.write_text(shared_member_program(512), encoding="utf-8")
        result = subprocess.run(
            [str(compiler), str(source), "-o", str(executable)],
            capture_output=True, text=True, timeout=timeout,
        )
        assert result.returncode == 0, result.stdout + result.stderr
        # A shared predecessor must not impose an order between plus and minus.
        assert "multiple valid operand groupings" in result.stdout + result.stderr, result.stdout + result.stderr
        result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=5, check=True)
        assert result.stdout == "31\n1\n6\n11\n", result.stdout

        cycles = {
            "choice_family": '''function left [woven|braided] right:
    replacement:
        @intrinsic("add", left, right)

function left woven right:
    before: $ braided $
    replacement:
        @intrinsic("add", left, right)
''',
            "default": '''function left woven right:
    before: default
    after: default
    replacement:
        @intrinsic("add", left, right)
''',
        }
        for name, program in cycles.items():
            source = root / f"{name}.dl"
            source.write_text(program, encoding="utf-8")
            result = subprocess.run(
                [str(compiler), str(source), "--emit-llvm", "-o", str(root / f"{name}.ll")],
                capture_output=True, text=True, timeout=timeout,
            )
            assert result.returncode != 0, f"{name}: precedence cycle was accepted"
            assert "Cycle detected in precedence declarations" in result.stdout + result.stderr, result.stdout + result.stderr
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
