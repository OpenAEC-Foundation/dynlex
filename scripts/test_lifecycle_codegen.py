#!/usr/bin/env python3

import pathlib
import re
import subprocess
import sys
import tempfile


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: test_lifecycle_codegen.py <compiler> <source>", file=sys.stderr)
        return 2

    compiler = pathlib.Path(sys.argv[1]).resolve()
    source = pathlib.Path(sys.argv[2]).resolve()
    with tempfile.TemporaryDirectory() as temporary_directory:
        llvm_path = pathlib.Path(temporary_directory) / "lifecycle.ll"
        result = subprocess.run(
            [str(compiler), str(source), "--emit-llvm", "-o", str(llvm_path)],
            capture_output=True,
            text=True,
            check=False,
        )
        if result.returncode != 0:
            sys.stderr.write(result.stdout)
            sys.stderr.write(result.stderr)
            return result.returncode
        llvm = llvm_path.read_text(encoding="utf-8")

    lifecycle_functions = re.findall(
        r'^define internal void @[^\n]*lifecycle_allocation_probe[^\n]*\{\n(?P<body>.*?)^\}',
        llvm,
        flags=re.MULTILINE | re.DOTALL,
    )
    if len(lifecycle_functions) != 2:
        print(f"expected two probe lifecycle functions, found {len(lifecycle_functions)}", file=sys.stderr)
        return 1

    for body in lifecycle_functions:
        temporary_allocations = re.findall(r'^\s+%temporary\d* = alloca ', body, flags=re.MULTILINE)
        initialization_flags = re.findall(r'^\s+%managed_initialized\d* = alloca i1', body, flags=re.MULTILINE)
        if len(temporary_allocations) != 1 or len(initialization_flags) != 1:
            print(
                "lifecycle function must allocate its managed local and initialization flag exactly once; "
                f"found {len(temporary_allocations)} locals and {len(initialization_flags)} flags",
                file=sys.stderr,
            )
            return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
