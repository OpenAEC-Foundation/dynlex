#!/usr/bin/env python3
from __future__ import annotations

import platform
import subprocess
import sys
import tempfile
from pathlib import Path


SCRIPTS_DIR = Path(__file__).resolve().parent


def check_incremental_source_locations(compiler, verifier, root):
    source = root / "incremental_debug.dl"
    original = ('to get next value:\n'
                '    @intrinsic("return", @intrinsic("add", value, 1))\n'
                'to report {value}:\n'
                '    @intrinsic("variadic call", "libc", "printf", '
                '@intrinsic("type", "nothing"), 1, "%d\\n", next value)\n'
                'report @intrinsic("command line argument count")\n')
    for optimization in ("-O0", "-O2"):
        cache = root / ("cache" + optimization)
        for index, text in enumerate((original,
                "# moved\n\n" + original.replace('"add", value, 1', '"add", value, 2'),
                "# moved again\n\n\n" + original)):
            source.write_text(text)
            dumps = []
            for incremental in ((True,) if index == 0 else (True, False)):
                output = root / ("incremental.out" if incremental else "clean.out")
                selected_cache = cache if incremental else root / f"fresh{optimization}-{index}"
                assert incremental or not selected_cache.exists()
                flags = ["--cache-dir", str(selected_cache)]
                subprocess.run([str(compiler), str(source), "-g", optimization, *flags,
                                "-o", str(output)], cwd=root, capture_output=True, check=True)
                subprocess.run([str(verifier), "--verify", str(output)],
                               capture_output=True, check=True)
                dump = subprocess.check_output([str(verifier), "--debug-info", "--debug-line",
                                                str(output)], text=True)
                lines = dump.splitlines()
                assert "file format" in lines[0], dump
                dumps.append(lines[1:])
            if index:
                assert dumps[0] == dumps[1], "incremental DWARF differs from current clean source"
    from test_incremental_debug_ir import debug_ir_global_edits, debug_ir_recursive_edits
    for optimization in ("-O0", "-O2"):
        for index, text in enumerate(debug_ir_global_edits() + debug_ir_recursive_edits()):
            source.write_text(text)
            dumps = []
            for mode in ("incremental", "fresh"):
                cache = root / f"globals-{optimization}-{mode}{index if mode == 'fresh' else ''}"
                output = root / f"globals-{mode}.out"
                subprocess.run([str(compiler), str(source), "-g", optimization, "--cache-dir", str(cache),
                                "-o", str(output)], cwd=root, capture_output=True, check=True)
                subprocess.run([str(verifier), "--verify", str(output)], capture_output=True, check=True)
                dumps.append(subprocess.check_output([str(verifier), "--debug-info", "--debug-line",
                                                      str(output)], text=True).splitlines()[1:])
            assert dumps[0] == dumps[1], "reused global/type DWARF differs from a fresh build"


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
        "debug_managed_cleanup",
    )]
    with tempfile.TemporaryDirectory(prefix="dynlex-debug-info-") as temporary_directory:
        from test_debug_source_origins import check_cross_file_local_debug_locations
        check_cross_file_local_debug_locations(compiler, Path(temporary_directory))
        check_incremental_source_locations(compiler, dwarf_verifier, Path(temporary_directory))
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
