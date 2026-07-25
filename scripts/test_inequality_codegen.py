#!/usr/bin/env python3

import pathlib
import re
import subprocess
import sys
import tempfile


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: test_inequality_codegen.py <compiler> <source>", file=sys.stderr)
        return 2

    compiler = pathlib.Path(sys.argv[1]).resolve()
    source = pathlib.Path(sys.argv[2]).resolve()
    with tempfile.TemporaryDirectory() as temporary_directory:
        llvm_path = pathlib.Path(temporary_directory) / "inequality.ll"
        result = subprocess.run(
            [str(compiler), str(source), "--emit-llvm", "-O0", "-o", str(llvm_path)],
            capture_output=True,
            text=True,
            check=False,
        )
        if result.returncode != 0:
            sys.stderr.write(result.stdout)
            sys.stderr.write(result.stderr)
            return result.returncode
        llvm = llvm_path.read_text(encoding="utf-8")

    if re.search(r"\bcall\s+i1\s+@not_value_", llvm):
        print("generic inequality emitted a runtime call to the standard not wrapper", file=sys.stderr)
        return 1
    if not re.search(r"\bxor\s+i1\b", llvm):
        print("generic inequality did not lower boolean negation inline", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
