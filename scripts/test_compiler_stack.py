#!/usr/bin/env python3
"""Compile nested pattern calls within the Windows default stack budget."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

compiler = Path(sys.argv[1]).resolve()
depth = 48
replacement_program = "".join(
    f'function step{index} value:\n    replacement:\n'
    f'        @intrinsic("add", value, (step{index + 1} value))\n\n'
    for index in range(depth)
)
replacement_program += f'function step{depth} value:\n    replacement:\n        @intrinsic("add", value, 0)\n\n'
replacement_program += '@intrinsic("return", step0 1)\n'
intrinsic_depth = 72
intrinsic_program = (
    '@intrinsic("return", ' + '@intrinsic("add", 1, ' * intrinsic_depth + '0' + ')' * (intrinsic_depth + 1) + '\n'
)
programs = [("intrinsics", intrinsic_program, intrinsic_depth), ("replacements", replacement_program, depth + 1)]

options = {}
if os.name != "nt":
    import resource

    def limit_stack():
        _, hard_limit = resource.getrlimit(resource.RLIMIT_STACK)
        # Leave headroom for the larger Windows ABI frames within its 1 MiB default.
        resource.setrlimit(resource.RLIMIT_STACK, (768 * 1024, hard_limit))

    options["preexec_fn"] = limit_stack

with tempfile.TemporaryDirectory(prefix="dynlex-compiler-stack-") as directory:
    for name, program, expected in programs:
        source = Path(directory) / f"{name}.dl"
        output = Path(directory) / (name + (".exe" if os.name == "nt" else ".out"))
        source.write_text(program)
        result = subprocess.run(
            [str(compiler), str(source), "--no-incremental", "-o", str(output)],
            capture_output=True, text=True, timeout=30, **options,
        )
        assert result.returncode == 0, (name, result.returncode, result.stdout + result.stderr)
        executed = subprocess.run([str(output)], capture_output=True, text=True, timeout=5)
        assert executed.returncode == expected, (name, executed.returncode, executed.stderr)
        print(f"compiler stack ({name}): passed")
