#!/usr/bin/env python3
"""Compile nested pattern calls within the Windows default stack budget."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

compiler = Path(sys.argv[1]).resolve()
depth = 48
program = "".join(
    f'function step{index} value:\n    replacement:\n'
    f'        @intrinsic("add", value, (step{index + 1} value))\n\n'
    for index in range(depth)
)
program += f'function step{depth} value:\n    replacement:\n        @intrinsic("add", value, 0)\n\n'
program += '@intrinsic("return", step0 1)\n'

options = {}
if os.name != "nt":
    import resource

    def limit_stack():
        _, hard_limit = resource.getrlimit(resource.RLIMIT_STACK)
        resource.setrlimit(resource.RLIMIT_STACK, (1024 * 1024, hard_limit))

    options["preexec_fn"] = limit_stack

with tempfile.TemporaryDirectory(prefix="dynlex-compiler-stack-") as directory:
    source = Path(directory) / "main.dl"
    output = Path(directory) / ("main.exe" if os.name == "nt" else "main.out")
    source.write_text(program)
    result = subprocess.run(
        [str(compiler), str(source), "-o", str(output)],
        capture_output=True, text=True, timeout=30, **options,
    )
    assert result.returncode == 0, (result.returncode, result.stdout + result.stderr)
    executed = subprocess.run([str(output)], capture_output=True, text=True, timeout=5)
    assert executed.returncode == 49, (executed.returncode, executed.stderr)
    print("compiler stack: passed")
