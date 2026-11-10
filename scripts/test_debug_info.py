#!/usr/bin/env python3
from __future__ import annotations

import platform
import subprocess
import sys
import tempfile
from pathlib import Path


SCRIPTS_DIR = Path(__file__).resolve().parent


def resolve_native_llvm_install_directory() -> Path | None:
    completed = subprocess.run(
        [
            "bash",
            "-c",
            '. "$1"; printf "%s\\n" "$DYNLEX_LLVM_NATIVE_INSTALL_DIR"',
            "bash",
            str(SCRIPTS_DIR / "llvm_toolchain.sh"),
        ],
        text=True,
        capture_output=True,
        check=False,
    )
    if completed.returncode != 0:
        print(completed.stdout + completed.stderr, file=sys.stderr)
        return None
    return Path(completed.stdout.strip())


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_debug_info.py <compiler>", file=sys.stderr)
        return 2

    if platform.system() != "Linux":
        return 0

    compiler = Path(sys.argv[1]).resolve()
    if not compiler.is_file():
        print(f"compiler not found: {compiler}", file=sys.stderr)
        return 2

    repo_root = SCRIPTS_DIR.parent
    llvm_install_directory = resolve_native_llvm_install_directory()
    if llvm_install_directory is None:
        return 2
    dwarf_verifier = llvm_install_directory / "bin" / "llvm-dwarfdump"
    if not dwarf_verifier.is_file():
        print(f"required pinned DWARF verifier not found: {dwarf_verifier}", file=sys.stderr)
        return 2

    sources = [repo_root / "tests/required" / name / "main.dl" for name in (
        "simple", "debug_class_globals", "debug_empty_function", "recursive_class_pointer", "recursive_class_double_pointer",
    )]
    with tempfile.TemporaryDirectory(prefix="dynlex-debug-info-") as temporary_directory:
        for source in sources:
            executable = Path(temporary_directory) / (source.parent.name + ".out")
            compile_result = subprocess.run(
                [str(compiler), str(source), "-g", "-O0", "-o", str(executable)],
                cwd=repo_root, text=True, capture_output=True, check=False,
            )
            if compile_result.returncode != 0:
                print(compile_result.stdout + compile_result.stderr, file=sys.stderr)
                return 1
            verify_result = subprocess.run(
                [str(dwarf_verifier), "--verify", str(executable)],
                text=True, capture_output=True, check=False,
            )
            if verify_result.returncode != 0:
                print(verify_result.stdout + verify_result.stderr, file=sys.stderr)
                return 1
            if source.parent.name == "debug_class_globals":
                ir = executable.with_suffix(".ll")
                subprocess.run([str(compiler), str(source), "-g", "-O0", "--emit-llvm", "-o", str(ir)],
                               cwd=repo_root, check=True, capture_output=True, text=True)
                metadata = ir.read_text()
                for name in ("global", "chain"):
                    definition = next(line for line in metadata.splitlines() if line.startswith(f"@{name} ="))
                    if "!dbg" not in definition:
                        raise AssertionError(f"{name} has no attached debug information")
                for name in ("record", "node"):
                    if f'DICompositeType(tag: DW_TAG_structure_type, name: "{name}"' not in metadata:
                        raise AssertionError(f"The {name} structure has no debug type")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
