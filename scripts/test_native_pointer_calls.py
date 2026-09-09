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

exposed function call zero arguments {a pointer:callee}:
    execute:
        return @intrinsic("call pointer", callee, a 64 bit unsigned integer)

exposed function call addition {a pointer:callee} with {a 32 bit integer:left} and {a 32 bit integer:right}:
    execute:
        return @intrinsic("call pointer", callee, a 32 bit integer, left, right)

exposed function call increment {a pointer:callee} with {a pointer to a 32 bit integer:value}:
    execute:
        @intrinsic("call pointer", callee, nothing, value)

exposed function call float32 {a pointer:callee} with {a 32 bit floating-point number:value}:
    execute:
        return @intrinsic("call pointer", callee, a 32 bit floating-point number, value)

exposed function call float64 {a pointer:callee} with {a 64 bit floating-point number:value}:
    execute:
        return @intrinsic("call pointer", callee, a 64 bit floating-point number, value)

exposed function call pointer result {a pointer:callee} with {a pointer to a byte:value}:
    execute:
        return @intrinsic("call pointer", callee, a pointer to a byte, value)

exposed function call predicate {a pointer:callee} with {boolean:value}:
    execute:
        return @intrinsic("call pointer", callee, a boolean, value)

exposed function call signed octet result {a pointer:callee}:
    execute:
        return @intrinsic("call pointer", callee, an 8 bit integer)

exposed function call unsigned octet result {a pointer:callee}:
    execute:
        return @intrinsic("call pointer", callee, an 8 bit unsigned integer)

exposed function call signed halfword result {a pointer:callee}:
    execute:
        return @intrinsic("call pointer", callee, a 16 bit integer)

exposed function call unsigned halfword result {a pointer:callee}:
    execute:
        return @intrinsic("call pointer", callee, a 16 bit unsigned integer)

exposed function call scalar {a pointer:callee} with {boolean:flag}, {8 bit integer:signed octet}, {8 bit unsigned integer:unsigned octet}, {16 bit integer:signed halfword}, {16 bit unsigned integer:unsigned halfword} and {64 bit unsigned integer:wide}:
    execute:
        return @intrinsic("call pointer", callee, a 64 bit unsigned integer, flag, signed octet, unsigned octet, signed halfword, unsigned halfword, wide)
"""

TARGET_SOURCE = """\
import lib/std.dl
set value to 0
@intrinsic("call pointer", the address of value, nothing)
"""

NEGATIVE_SOURCES = {
    "non-pointer-callee": (
        'import lib/std.dl\n@intrinsic("discard", @intrinsic("call pointer", 1, an integer))\n',
        "call pointer callee must be a native pointer",
    ),
    "non-type-return": (
        'import lib/std.dl\n@intrinsic("discard", @intrinsic("call pointer", 0 as a pointer, 1))\n',
        "call return type must be a compile-time type reference",
    ),
    "unresolved-return": (
        'import lib/std.dl\n@intrinsic("discard", @intrinsic("call pointer", 0 as a pointer, @intrinsic("type", "any")))\n',
        "call return type must be a concrete runtime type",
    ),
}


def run(arguments: list[str], working_directory: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(arguments, cwd=working_directory, text=True, capture_output=True, check=False)


def require_success(result: subprocess.CompletedProcess[str]) -> None:
    if result.returncode != 0:
        raise RuntimeError(f"command {result.args} exited {result.returncode}:\n{result.stdout}{result.stderr}")


def require_failure(result: subprocess.CompletedProcess[str], expected: str) -> None:
    if result.returncode == 0:
        raise RuntimeError(f"compiler unexpectedly accepted {result.args}")
    output = result.stdout + result.stderr
    if expected not in output:
        raise RuntimeError(f"expected {expected!r} in compiler output:\n{output}")


def resolve_c_compiler(compiler: Path) -> str:
    cache_path = compiler.parent / "CMakeCache.txt"
    if cache_path.is_file():
        for line in cache_path.read_text(encoding="utf-8").splitlines():
            match = re.fullmatch(r"CMAKE_C_COMPILER:[^=]+=(.+)", line)
            if match and Path(match.group(1)).is_file():
                return match.group(1)
    return os.environ.get("CC", "cc")


def exposed_symbol(llvm_ir: str, function_name: str) -> str:
    match = re.search(
        rf"^define .* @([^( ]*{function_name.replace(' ', '_')}[^ (]*_callable[^ (]*)\(", llvm_ir, re.MULTILINE
    )
    if not match:
        raise RuntimeError(f"LLVM output omitted exposed function {function_name!r}:\n{llvm_ir}")
    return match.group(1)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_native_pointer_calls.py <compiler>", file=sys.stderr)
        return 2
    compiler = Path(sys.argv[1]).resolve()
    if not compiler.is_file():
        print(f"compiler not found: {compiler}", file=sys.stderr)
        return 2

    repo_root = Path(__file__).resolve().parent.parent
    c_compiler = resolve_c_compiler(compiler)
    try:
        with tempfile.TemporaryDirectory(prefix="dynlex-native-pointer-calls-") as temporary_directory:
            temporary = Path(temporary_directory)
            library = temporary / "library.dl"
            library.write_text(LIBRARY_SOURCE, encoding="utf-8")
            llvm_output = temporary / "library.ll"
            require_success(run([str(compiler), str(library), "--emit-llvm", "--no-main", "-o", str(llvm_output)], repo_root))
            llvm_ir = llvm_output.read_text(encoding="utf-8")
            add_symbol = exposed_symbol(llvm_ir, "call addition")
            increment_symbol = exposed_symbol(llvm_ir, "call increment")
            float32_symbol = exposed_symbol(llvm_ir, "call float32")
            float64_symbol = exposed_symbol(llvm_ir, "call float64")
            pointer_symbol = exposed_symbol(llvm_ir, "call pointer result")
            predicate_symbol = exposed_symbol(llvm_ir, "call predicate")
            signed_octet_symbol = exposed_symbol(llvm_ir, "call signed octet result")
            unsigned_octet_symbol = exposed_symbol(llvm_ir, "call unsigned octet result")
            signed_halfword_symbol = exposed_symbol(llvm_ir, "call signed halfword result")
            unsigned_halfword_symbol = exposed_symbol(llvm_ir, "call unsigned halfword result")
            scalar_symbol = exposed_symbol(llvm_ir, "call scalar")
            zero_symbol = exposed_symbol(llvm_ir, "call zero arguments")
            if not re.search(r"call i32 %callee_val\(i32 %left_val, i32 %right_val\)", llvm_ir):
                raise RuntimeError("call pointer did not emit an indirect SSA call:\n" + llvm_ir)

            target = re.search(r'^target triple = "([^"]+)"', llvm_ir, re.MULTILINE)
            if not target:
                raise RuntimeError("native LLVM output omitted its target triple")
            triple = target.group(1)
            uses_aapcs64 = triple.startswith(("aarch64", "arm64")) and "apple" not in triple
            uses_win64 = triple.startswith("x86_64") and "windows" in triple
            bool_extension = "" if uses_aapcs64 else "zeroext "
            integer_extension = "" if uses_aapcs64 or uses_win64 else "signext "
            unsigned_extension = "" if uses_aapcs64 or uses_win64 else "zeroext "
            if not re.search(rf"call {bool_extension}i1 %callee_val\(i1 {bool_extension}%value_val\)", llvm_ir):
                raise RuntimeError("indirect Boolean call omitted its native ABI contract")
            for width, extension, name in (
                (8, integer_extension, "signed octet"),
                (8, unsigned_extension, "unsigned octet"),
                (16, integer_extension, "signed halfword"),
                (16, unsigned_extension, "unsigned halfword"),
            ):
                if not re.search(rf"call {extension}i{width} %callee_val\(\)", llvm_ir):
                    raise RuntimeError(f"indirect {name} return omitted its native ABI contract")
            scalar_call = re.search(r"call (?:zeroext )?i64 %callee_val\((i1 [^)]*)\)", llvm_ir)
            if not scalar_call or f"i1 {bool_extension}" not in scalar_call.group(1) or f"i8 {integer_extension}" not in scalar_call.group(1) or f"i8 {unsigned_extension}" not in scalar_call.group(1) or f"i16 {integer_extension}" not in scalar_call.group(1) or f"i16 {unsigned_extension}" not in scalar_call.group(1):
                raise RuntimeError("indirect narrow scalar call omitted its native ABI contract")

            caller = temporary / "caller.c"
            caller.write_text(
                f"""\
#include <stdbool.h>
#include <stdint.h>

extern uint64_t zero(void *) __asm__("{zero_symbol}");
extern int32_t add(void *, int32_t, int32_t) __asm__("{add_symbol}");
extern void increment(void *, int32_t *) __asm__("{increment_symbol}");
extern float float32(void *, float) __asm__("{float32_symbol}");
extern double float64(void *, double) __asm__("{float64_symbol}");
extern void *pointer_result(void *, void *) __asm__("{pointer_symbol}");
extern bool predicate(void *, bool) __asm__("{predicate_symbol}");
extern int8_t signed_octet_result(void *) __asm__("{signed_octet_symbol}");
extern uint8_t unsigned_octet_result(void *) __asm__("{unsigned_octet_symbol}");
extern int16_t signed_halfword_result(void *) __asm__("{signed_halfword_symbol}");
extern uint16_t unsigned_halfword_result(void *) __asm__("{unsigned_halfword_symbol}");
extern uint64_t scalar(void *, bool, int8_t, uint8_t, int16_t, uint16_t, uint64_t) __asm__("{scalar_symbol}");

static uint64_t return_max(void) {{ return UINT64_MAX; }}
static int32_t add_values(int32_t left, int32_t right) {{ return left + right; }}
static void increment_value(int32_t *value) {{ ++*value; }}
static float double_float(float value) {{ return value * 2.0f; }}
static double halve_double(double value) {{ return value / 2.0; }}
static void *identity_pointer(void *value) {{ return value; }}
static bool negate(bool value) {{ return !value; }}
static int8_t return_signed_octet(void) {{ return INT8_MIN; }}
static uint8_t return_unsigned_octet(void) {{ return UINT8_MAX; }}
static int16_t return_signed_halfword(void) {{ return INT16_MIN; }}
static uint16_t return_unsigned_halfword(void) {{ return UINT16_MAX; }}
static uint64_t check_scalars(bool flag, int8_t signed_octet, uint8_t unsigned_octet, int16_t signed_halfword, uint16_t unsigned_halfword, uint64_t wide) {{
    return flag && signed_octet == INT8_MIN && unsigned_octet == UINT8_MAX && signed_halfword == INT16_MIN && unsigned_halfword == UINT16_MAX && wide == UINT64_C(0xfedcba9876543210) ? wide : 0;
}}

int main(void) {{
    int32_t value = 41;
    if (zero((void *)return_max) != UINT64_MAX) return 1;
    if (add((void *)add_values, 19, 23) != 42) return 2;
    increment((void *)increment_value, &value);
    if (value != 42) return 3;
    if (float32((void *)double_float, 1.25f) != 2.5f) return 4;
    if (float64((void *)halve_double, 9.0) != 4.5) return 5;
    if (pointer_result((void *)identity_pointer, &value) != &value) return 6;
    if (predicate((void *)negate, false) != true || predicate((void *)negate, true) != false) return 7;
    if (signed_octet_result((void *)return_signed_octet) != INT8_MIN) return 8;
    if (unsigned_octet_result((void *)return_unsigned_octet) != UINT8_MAX) return 9;
    if (signed_halfword_result((void *)return_signed_halfword) != INT16_MIN) return 10;
    if (unsigned_halfword_result((void *)return_unsigned_halfword) != UINT16_MAX) return 11;
    if (scalar((void *)check_scalars, true, INT8_MIN, UINT8_MAX, INT16_MIN, UINT16_MAX, UINT64_C(0xfedcba9876543210)) != UINT64_C(0xfedcba9876543210)) return 12;
    return 0;
}}
""",
                encoding="utf-8",
            )
            program = temporary / ("caller.exe" if os.name == "nt" else "caller")
            object_output = temporary / "library.o"
            for optimization in ("-O0", "-O2", "-O3"):
                require_success(
                    run(
                        [str(compiler), str(library), "--emit-object", "--no-main", optimization, "-o", str(object_output)],
                        repo_root,
                    )
                )
                require_success(run([c_compiler, "-O2", str(caller), str(object_output), "-o", str(program)], repo_root))
                require_success(run([str(program)], repo_root))

            for name, (source, expected) in NEGATIVE_SOURCES.items():
                source_path = temporary / f"{name}.dl"
                source_path.write_text(source, encoding="utf-8")
                require_failure(run([str(compiler), str(source_path), "--emit-llvm"], repo_root), expected)

            target_source = temporary / "target.dl"
            target_source.write_text(TARGET_SOURCE, encoding="utf-8")
            for arguments in (("--emit-wasm",), ("--emit-spirv", "--shader-stage=fragment")):
                require_failure(
                    run([str(compiler), str(target_source), *arguments], repo_root),
                    "Intrinsic 'call pointer' is only available when emitting native CPU code",
                )
    except RuntimeError as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
