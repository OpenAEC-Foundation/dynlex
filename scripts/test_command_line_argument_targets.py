#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path


EXPECTED_DIAGNOSTIC = "Command-line arguments are unavailable for this target"
TARGETS = (
    ("wasm", ("--emit-wasm",)),
    ("spirv", ("--emit-spirv", "--shader-stage=fragment")),
)
INTRINSICS = (
    "command line argument count",
    "command line argument values",
)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_command_line_argument_targets.py <compiler>", file=sys.stderr)
        return 2

    compiler = Path(sys.argv[1]).resolve()
    if not compiler.is_file():
        print(f"compiler not found: {compiler}", file=sys.stderr)
        return 2

    failures: list[str] = []
    with tempfile.TemporaryDirectory(prefix="dynlex-command-line-targets-") as temporary_directory:
        temporary_path = Path(temporary_directory)
        for target_name, target_arguments in TARGETS:
            for intrinsic_name in INTRINSICS:
                source_path = temporary_path / f"{target_name}-{intrinsic_name.rsplit(' ', 1)[-1]}.dl"
                output_path = temporary_path / f"{target_name}-{intrinsic_name.rsplit(' ', 1)[-1]}.out"
                source_path.write_text(
                    f'@intrinsic("discard", @intrinsic("{intrinsic_name}"))\n',
                    encoding="utf-8",
                )
                result = subprocess.run(
                    [str(compiler), str(source_path), *target_arguments, "-o", str(output_path)],
                    text=True,
                    capture_output=True,
                    check=False,
                )
                diagnostics = result.stdout + result.stderr
                if result.returncode == 0:
                    failures.append(f"{target_name} accepted intrinsic '{intrinsic_name}'")
                elif EXPECTED_DIAGNOSTIC not in diagnostics:
                    failures.append(
                        f"{target_name} intrinsic '{intrinsic_name}' produced the wrong diagnostic: {diagnostics.strip()}"
                    )

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
