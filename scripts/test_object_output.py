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

exposed function predicate {32 bit floating-point number:left} above {32 bit floating-point number:right} minimum {32 bit floating-point number:minimum}:
    execute:
        return (left > (0.0 as a 32 bit floating-point number)) and ((left - right) >= minimum)

exposed function negate {boolean:flag}:
    execute:
        return the inverse of flag

exposed function foreign predicate {32 bit floating-point number:left} above {32 bit floating-point number:right} minimum {32 bit floating-point number:minimum}:
    execute:
        set result to predicate left above right minimum minimum
        return @intrinsic("call", "", "bool_value", a 32 bit integer, result)

exposed function signed octet {8 bit integer:value}:
    execute:
        return value

exposed function signed halfword {16 bit integer:value}:
    execute:
        return value

exposed function foreign signed octet {8 bit integer:value}:
    execute:
        return @intrinsic("call", "", "octet_value", a 32 bit integer, value)

exposed function unsigned octet {8 bit unsigned integer:value}:
    execute:
        return value

exposed function unsigned halfword {16 bit unsigned integer:value}:
    execute:
        return value

exposed function foreign unsigned octet {8 bit unsigned integer:value}:
    execute:
        return @intrinsic("call", "", "unsigned_octet_value", a 32 bit unsigned integer, value)

exposed function foreign unsigned halfword {16 bit unsigned integer:value}:
    execute:
        return @intrinsic("call", "", "unsigned_halfword_value", a 32 bit unsigned integer, value)

exposed function foreign unsigned result:
    execute:
        return @intrinsic("call", "", "unsigned_halfword_result", a 16 bit unsigned integer)
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
PREDICATE_SYMBOL = "predicate_332_bit_floating5point_number8left5_above_332_bit_floating5point_number8right5_minimum_332_bit_floating5point_number8minimum5_callable_f32_f32_f32"
NEGATE_SYMBOL = "negate_3boolean8flag5_callable_bool"
FOREIGN_PREDICATE_SYMBOL = "foreign_" + PREDICATE_SYMBOL
OCTET_SYMBOL = "signed_octet_38_bit_integer8value5_callable_i8"
HALFWORD_SYMBOL = "signed_halfword_316_bit_integer8value5_callable_i16"
FOREIGN_OCTET_SYMBOL = "foreign_" + OCTET_SYMBOL
UNSIGNED_OCTET_SYMBOL = "unsigned_octet_38_bit_unsigned_integer8value5_callable_u8"
UNSIGNED_HALFWORD_SYMBOL = "unsigned_halfword_316_bit_unsigned_integer8value5_callable_u16"
FOREIGN_UNSIGNED_OCTET_SYMBOL = "foreign_" + UNSIGNED_OCTET_SYMBOL
FOREIGN_UNSIGNED_HALFWORD_SYMBOL = "foreign_" + UNSIGNED_HALFWORD_SYMBOL
FOREIGN_UNSIGNED_RESULT_SYMBOL = "foreign_unsigned_result_callable"


def run(arguments: list[str], working_directory: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(arguments, cwd=working_directory, text=True, capture_output=True, check=False)


def require_success(result: subprocess.CompletedProcess[str]) -> None:
    if result.returncode != 0:
        raise RuntimeError(f"command {result.args} exited {result.returncode}:\n{result.stdout}{result.stderr}")


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
#include <stdbool.h>

extern int32_t {SUM_SYMBOL}(int32_t left, int32_t right);
extern void {INCREMENT_SYMBOL}(int32_t *value);
extern bool {PREDICATE_SYMBOL}(float left, float right, float minimum);
extern bool {NEGATE_SYMBOL}(bool value);
extern int32_t {FOREIGN_PREDICATE_SYMBOL}(float left, float right, float minimum);
extern int8_t {OCTET_SYMBOL}(int8_t value);
extern int16_t {HALFWORD_SYMBOL}(int16_t value);
extern int32_t {FOREIGN_OCTET_SYMBOL}(int8_t value);
extern uint8_t {UNSIGNED_OCTET_SYMBOL}(uint8_t value);
extern uint16_t {UNSIGNED_HALFWORD_SYMBOL}(uint16_t value);
extern uint32_t {FOREIGN_UNSIGNED_OCTET_SYMBOL}(uint8_t value);
extern uint32_t {FOREIGN_UNSIGNED_HALFWORD_SYMBOL}(uint16_t value);
extern uint16_t {FOREIGN_UNSIGNED_RESULT_SYMBOL}(void);

int32_t bool_value(bool value) {{ return value; }}
int32_t octet_value(int8_t value) {{ return value; }}
uint32_t unsigned_octet_value(uint8_t value) {{ return value; }}
uint32_t unsigned_halfword_value(uint16_t value) {{ return value; }}
uint16_t unsigned_halfword_result(void) {{ return UINT16_MAX; }}

int main(void) {{
    int32_t value = 41;
    {INCREMENT_SYMBOL}(&value);
    if ({SUM_SYMBOL}(19, 23) != 42 || value != 42) return 1;
    if ((int){PREDICATE_SYMBOL}(100.0f, 98.8f, 1.0f) != 1) return 2;
    if ((int){PREDICATE_SYMBOL}(100.0f, 99.4f, 1.0f) != 0) return 3;
    if ((int){NEGATE_SYMBOL}(true) != 0 || (int){NEGATE_SYMBOL}(false) != 1) return 4;
    if ({FOREIGN_PREDICATE_SYMBOL}(100.0f, 98.8f, 1.0f) != 1) return 5;
    if ({FOREIGN_PREDICATE_SYMBOL}(100.0f, 99.4f, 1.0f) != 0) return 6;
    if ((int){OCTET_SYMBOL}(-128) != -128 || (int){OCTET_SYMBOL}(127) != 127) return 7;
    if ((int){HALFWORD_SYMBOL}(-32768) != -32768 || (int){HALFWORD_SYMBOL}(32767) != 32767) return 8;
    if ({FOREIGN_OCTET_SYMBOL}(-128) != -128 || {FOREIGN_OCTET_SYMBOL}(127) != 127) return 9;
    if ((int){UNSIGNED_OCTET_SYMBOL}(0) != 0 || (int){UNSIGNED_OCTET_SYMBOL}(UINT8_MAX) != UINT8_MAX) return 10;
    if ((int){UNSIGNED_HALFWORD_SYMBOL}(0) != 0 || (int){UNSIGNED_HALFWORD_SYMBOL}(UINT16_MAX) != UINT16_MAX) return 11;
    if ({FOREIGN_UNSIGNED_OCTET_SYMBOL}(UINT8_MAX) != UINT8_MAX) return 12;
    if ({FOREIGN_UNSIGNED_HALFWORD_SYMBOL}(UINT16_MAX) != UINT16_MAX) return 13;
    if ((int){FOREIGN_UNSIGNED_RESULT_SYMBOL}() != UINT16_MAX) return 14;
    return 0;
}}
""",
                encoding="utf-8",
            )
            caller_program = temporary / ("caller.exe" if os.name == "nt" else "caller")
            for optimization in ("-O0", "-O2", "-O3"):
                require_success(run([str(compiler), str(library), "--emit-object", "--no-main", optimization,
                                     "-o", str(default_object)], repo_root))
                require_success(run([c_compiler, "-O2", str(caller), str(default_object), "-o", str(caller_program)], repo_root))
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
            target = re.search(r'^target triple = "([^"]+)"', llvm_ir, flags=re.MULTILINE)
            if not target:
                raise RuntimeError("native LLVM output omitted its target triple")
            triple = target.group(1)
            uses_aapcs64 = triple.startswith(("aarch64", "arm64")) and "apple" not in triple
            extension = "" if uses_aapcs64 else "zeroext "
            if not re.search(rf"^define {extension}i1 @{PREDICATE_SYMBOL}\(", llvm_ir, flags=re.MULTILINE):
                raise RuntimeError("exposed Boolean result omitted its native ABI contract")
            if not re.search(rf"^define {extension}i1 @{NEGATE_SYMBOL}\(i1 {extension}%flag\)", llvm_ir, flags=re.MULTILINE):
                raise RuntimeError("exposed Boolean argument omitted its native ABI contract")
            if not re.search(rf"call i32 @bool_value\(i1 {extension}", llvm_ir):
                raise RuntimeError("foreign Boolean call omitted its native ABI contract")
            uses_win64 = triple.startswith("x86_64") and "windows" in triple
            integer_extension = "" if uses_aapcs64 or uses_win64 else "signext "
            if not re.search(rf"^define {integer_extension}i8 @{OCTET_SYMBOL}\(i8 {integer_extension}%value\)", llvm_ir, flags=re.MULTILINE):
                raise RuntimeError("exposed narrow integer omitted its native ABI contract")
            if not re.search(rf"call i32 @octet_value\(i8 {integer_extension}", llvm_ir):
                raise RuntimeError("foreign narrow integer call omitted its native ABI contract")
            unsigned_extension = "" if uses_aapcs64 or uses_win64 else "zeroext "
            for width, symbol in ((8, UNSIGNED_OCTET_SYMBOL), (16, UNSIGNED_HALFWORD_SYMBOL)):
                if not re.search(rf"^define {unsigned_extension}i{width} @{symbol}\(i{width} {unsigned_extension}%value\)", llvm_ir, flags=re.MULTILINE):
                    raise RuntimeError("exposed unsigned integer omitted its native ABI contract")
            if not re.search(rf"call i32 @unsigned_octet_value\(i8 {unsigned_extension}", llvm_ir):
                raise RuntimeError("foreign unsigned argument omitted its native ABI contract")
            if not re.search(rf"call {unsigned_extension}i16 @unsigned_halfword_result\(\)", llvm_ir):
                raise RuntimeError("foreign unsigned result omitted its native ABI contract")

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
