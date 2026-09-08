#!/usr/bin/env python3

from __future__ import annotations

import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path


LIBRARY_SOURCE = """\
import lib/std.dl

exposed function sum {a 32 bit integer:left} and {a 32 bit integer:right}:
    execute:
        return left + right

exposed function increment {a pointer to a 32 bit integer:value}:
    execute:
        @intrinsic("store at", value, @intrinsic("add", @intrinsic("dereference", value), 1))
"""

EXECUTABLE_SOURCE = """\
import lib/std.dl

print 42 as a line
"""

COMMAND_LINE_LIBRARY_SOURCE = """\
import lib/std.dl

exposed function the argument count:
    execute:
        return @intrinsic("command line argument count")
"""

TOP_LEVEL_SECTION_SOURCE = LIBRARY_SOURCE + """
if true:
    print 1 as a line
"""

NEGATIVE_FIXED_VALUE_SOURCE = """\
import lib/std.dl

function the fixed value {fixed integer:value}:
    execute:
        return value

ignore the fixed value -1
"""

SUM_SYMBOL = "sum_3a_32_bit_integer8left5_and_3a_32_bit_integer8right5_callable_i32_i32"
INCREMENT_SYMBOL = "increment_3a_pointer_to_a_32_bit_integer8value5_callable_i32_2a_"


def run(arguments: list[str], working_directory: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(arguments, cwd=working_directory, text=True, capture_output=True, check=False)


def require_success(result: subprocess.CompletedProcess[str]) -> None:
    if result.returncode != 0:
        raise RuntimeError(result.stdout + result.stderr)


def require_failure(result: subprocess.CompletedProcess[str], message: str) -> None:
    if result.returncode == 0:
        raise RuntimeError(f"compiler unexpectedly accepted {result.args}")
    output = result.stdout + result.stderr
    if message not in output:
        raise RuntimeError(f"expected {message!r} in compiler output:\n{output}")


def resolve_c_compiler(compiler: Path) -> str:
    cache_path = compiler.parent / "CMakeCache.txt"
    if cache_path.is_file():
        for line in cache_path.read_text(encoding="utf-8").splitlines():
            match = re.fullmatch(r"CMAKE_C_COMPILER:[^=]+=(.+)", line)
            if match and Path(match.group(1)).is_file():
                return match.group(1)
    return os.environ.get("CC", "cc")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_object_output.py <compiler>", file=sys.stderr)
        return 2

    compiler = Path(sys.argv[1]).resolve()
    if not compiler.is_file():
        print(f"compiler not found: {compiler}", file=sys.stderr)
        return 2
    repo_root = Path(__file__).resolve().parent.parent
    # The CMake-selected compiler is the native ABI linker for this build on
    # every supported platform, including the pinned Windows toolchain.
    c_compiler = resolve_c_compiler(compiler)

    try:
        with tempfile.TemporaryDirectory(prefix="dynlex-object-output-") as temporary_directory:
            temporary = Path(temporary_directory)
            library = temporary / "library.dl"
            library.write_text(LIBRARY_SOURCE, encoding="utf-8")

            default_object = temporary / "library.o"
            result = run([str(compiler), str(library), "--emit-object", "--no-main", "-g"], repo_root)
            require_success(result)
            if not default_object.is_file():
                raise RuntimeError("--emit-object did not use the default .o output path")

            caller = temporary / "caller.c"
            caller.write_text(
                f"""\
#include <stdint.h>

extern int32_t {SUM_SYMBOL}(int32_t left, int32_t right);
extern void {INCREMENT_SYMBOL}(int32_t *value);

int main(void) {{
    int32_t value = 41;
    {INCREMENT_SYMBOL}(&value);
    return {SUM_SYMBOL}(19, 23) == 42 && value == 42 ? 0 : 1;
}}
""",
                encoding="utf-8",
            )
            caller_program = temporary / ("caller.exe" if os.name == "nt" else "caller")
            require_success(run([c_compiler, str(caller), str(default_object), "-o", str(caller_program)], repo_root))
            require_success(run([str(caller_program)], repo_root))

            llvm_output = temporary / "library.ll"
            require_success(
                run([str(compiler), str(library), "--emit-llvm", "--no-main", "-o", str(llvm_output)], repo_root)
            )
            llvm_ir = llvm_output.read_text(encoding="utf-8")
            if re.search(r"^define .*@main\(", llvm_ir, flags=re.MULTILINE):
                raise RuntimeError("definition-only LLVM output defines main")
            if f'@{SUM_SYMBOL}' not in llvm_ir or f'@{INCREMENT_SYMBOL}' not in llvm_ir:
                raise RuntimeError("definition-only LLVM output omitted exposed callable functions")

            negative_fixed_value = temporary / "negative-fixed-value.dl"
            negative_fixed_value.write_text(NEGATIVE_FIXED_VALUE_SOURCE, encoding="utf-8")
            negative_llvm_output = temporary / "negative-fixed-value.ll"
            require_success(
                run([str(compiler), str(negative_fixed_value), "--emit-llvm", "-o", str(negative_llvm_output)], repo_root)
            )
            if "_ct_value_i_2d_1" not in negative_llvm_output.read_text(encoding="utf-8"):
                raise RuntimeError("negative fixed integer generated an unsafe LLVM symbol suffix")

            require_failure(
                run([str(compiler), str(library), "--emit-object", "--emit-llvm"], repo_root),
                "Choose at most one explicit output mode",
            )
            require_failure(
                run([str(compiler), str(library), "--no-main"], repo_root), "--no-main requires --emit-llvm or --emit-object"
            )
            require_failure(
                run([str(compiler), str(library), "--emit-wasm", "--no-main"], repo_root),
                "--no-main requires --emit-llvm or --emit-object",
            )
            require_failure(
                run([str(compiler), "--emit-object", "print 1 as a line"], repo_root),
                "Command-line source supports native execution options only",
            )

            statement_library = temporary / "statement-library.dl"
            statement_library.write_text(LIBRARY_SOURCE + "\nprint 1 as a line\n", encoding="utf-8")
            require_failure(
                run([str(compiler), str(statement_library), "--emit-object", "--no-main"], repo_root),
                "definition-only output cannot contain executable top-level statements",
            )
            section_library = temporary / "section-library.dl"
            section_library.write_text(TOP_LEVEL_SECTION_SOURCE, encoding="utf-8")
            require_failure(
                run([str(compiler), str(section_library), "--emit-object", "--no-main"], repo_root),
                "definition-only output cannot contain executable top-level statements",
            )
            command_line_library = temporary / "command-line-library.dl"
            command_line_library.write_text(COMMAND_LINE_LIBRARY_SOURCE, encoding="utf-8")
            require_failure(
                run([str(compiler), str(command_line_library), "--emit-object", "--no-main"], repo_root),
                "Command-line arguments require a generated main function",
            )

            executable = temporary / "executable.dl"
            executable.write_text(EXECUTABLE_SOURCE, encoding="utf-8")
            program = temporary / ("executable.exe" if os.name == "nt" else "executable")
            require_success(run([str(compiler), str(executable), "-o", str(program)], repo_root))
            execution = run([str(program)], repo_root)
            require_success(execution)
            if execution.stdout != "42\n":
                raise RuntimeError(f"normal native executable output changed: {execution.stdout!r}")
    except RuntimeError as error:
        print(error, file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
