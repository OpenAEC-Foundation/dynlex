#!/usr/bin/env python3
"""Exercise native DynLex atomics against C11 atomic storage and threads."""

from __future__ import annotations

import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path


LIBRARY_SOURCE = """\
import lib/atomic.dl

exposed function increment {a pointer to a 64 bit unsigned integer:value}:
    execute:
        return @intrinsic("atomic fetch add", value, 1 as a 64 bit unsigned integer, "acq_rel")

exposed function load count {a pointer to a 64 bit unsigned integer:value}:
    execute:
        return @intrinsic("atomic load", value, "acquire")

exposed function publish {a pointer to a boolean:ready}:
    execute:
        @intrinsic("atomic store", ready, true, "release")

exposed function is ready {a pointer to a boolean:ready}:
    execute:
        return @intrinsic("atomic load", ready, "acquire")

exposed function exchange boolean {a pointer to a boolean:slot} with {boolean:value}:
    execute:
        return @intrinsic("atomic exchange", slot, value, "seq_cst")

exposed function add signed octet {a pointer to a 8 bit integer:slot}:
    execute:
        return @intrinsic("atomic fetch add", slot, 1 as a 8 bit integer, "relaxed")

exposed function subtract unsigned octet {a pointer to a 8 bit unsigned integer:slot}:
    execute:
        return @intrinsic("atomic fetch sub", slot, 1 as a 8 bit unsigned integer, "seq_cst")

exposed function exchange float {a pointer to a 32 bit floating-point number:slot} with {32 bit floating-point number:value}:
    execute:
        return @intrinsic("atomic exchange", slot, value, "seq_cst")

exposed function store float64 {a pointer to a 64 bit floating-point number:slot} with {64 bit floating-point number:value}:
    execute:
        @intrinsic("atomic store", slot, value, "relaxed")

exposed function exchange pointer {a pointer to a pointer to a byte:slot} with {a pointer to a byte:value}:
    execute:
        return @intrinsic("atomic exchange", slot, value, "seq_cst")

exposed function add signed halfword {a pointer to a 16 bit integer:slot}:
    execute:
        return @intrinsic("atomic fetch add", slot, 1 as a 16 bit integer, "relaxed")

exposed function subtract unsigned halfword {a pointer to a 16 bit unsigned integer:slot}:
    execute:
        return @intrinsic("atomic fetch sub", slot, 1 as a 16 bit unsigned integer, "seq_cst")

exposed function add signed word {a pointer to a 32 bit integer:slot}:
    execute:
        return @intrinsic("atomic fetch add", slot, 1 as a 32 bit integer, "acquire")

exposed function subtract unsigned word {a pointer to a 32 bit unsigned integer:slot}:
    execute:
        return @intrinsic("atomic fetch sub", slot, 1 as a 32 bit unsigned integer, "release")

exposed function add signed doubleword {a pointer to a 64 bit integer:slot}:
    execute:
        return @intrinsic("atomic fetch add", slot, 1 as a 64 bit integer, "seq_cst")

exposed function wrapped exchange {a pointer to a 32 bit unsigned integer:slot} with {32 bit unsigned integer:value}:
    execute:
        return atomically exchange value at slot

exposed function wrapped increment {a pointer to a 32 bit unsigned integer:slot}:
    execute:
        return atomically add acquire release (1 as a 32 bit unsigned integer) at slot

exposed function wrapped load {a pointer to a 32 bit unsigned integer:slot}:
    execute:
        return atomically load acquire slot

exposed function wrapped store {a pointer to a 32 bit unsigned integer:slot} with {32 bit unsigned integer:value}:
    execute:
        atomically store release value at slot
"""

STATE_TRACKING_SOURCE = """\
import lib/std.dl
set counter to 1
ignore @intrinsic("atomic fetch add", the address of counter, 1, "relaxed")
if counter = 1:
    print 1 as a line
else:
    print 2 as a line
"""

NEGATIVE_SOURCES = {
    "non-pointer": (
        '@intrinsic("atomic load", 1, "relaxed")\n',
        "Atomic operation requires a pointer to a concrete scalar",
    ),
    "load-release": (
        '@intrinsic("atomic load", 0 as a pointer, "release")\n',
        "Atomic memory order is invalid for this operation",
    ),
    "store-acquire": (
        '@intrinsic("atomic store", 0 as a pointer, 0, "acquire")\n',
        "Atomic memory order is invalid for this operation",
    ),
    "non-literal-order": (
        'set order to "relaxed"\n@intrinsic("atomic load", 0 as a pointer, order)\n',
        "must be a memory-order string literal",
    ),
    "bad-value": (
        '@intrinsic("atomic fetch add", 0 as a pointer, 1, "relaxed")\n',
        "Atomic operation value type must exactly match its pointer pointee",
    ),
    "float-fetch": (
        'set value to 1.0 as a 32 bit floating-point number\n'
        '@intrinsic("atomic fetch add", the address of value, 1.0 as a 32 bit floating-point number, "relaxed")\n',
        "Atomic fetch add and fetch sub require an integer pointee",
    ),
    "aggregate": (
        'set values to [1, 2]\n@intrinsic("atomic load", the address of values, "relaxed")\n',
        "Atomic operation requires a supported concrete scalar pointee",
    ),
    "managed": (
        'class:\n'
        '    patterns:\n'
        '        [a|] managed number\n'
        '    members:\n'
        '        value as an integer\n'
        '    retain:\n'
        '        ignore @intrinsic("lifecycle value")\n'
        '    release:\n'
        '        ignore @intrinsic("lifecycle value")\n'
        'set value to @intrinsic("construct", a managed number, 1)\n'
        '@intrinsic("atomic load", the address of value, "relaxed")\n',
        "Atomic operation requires a supported concrete scalar pointee",
    ),
    "float8": (
        'set value to 0 as @intrinsic("type", "float", 8)\n'
        '@intrinsic("atomic load", the address of value, "relaxed")\n',
        "Atomic operation requires a supported concrete scalar pointee",
    ),
    "float16": (
        'set value to 0 as @intrinsic("type", "float", 16)\n'
        '@intrinsic("atomic load", the address of value, "relaxed")\n',
        "Atomic operation requires a supported concrete scalar pointee",
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


def exposed_symbol(llvm_ir: str, function_name: str) -> str:
    match = re.search(
        rf"^define .* @([^( ]*{function_name.replace(' ', '_')}[^ (]*_callable[^ (]*)\(",
        llvm_ir,
        re.MULTILINE,
    )
    if not match:
        raise RuntimeError(f"LLVM output omitted exposed function {function_name!r}:\n{llvm_ir}")
    return match.group(1)


def require_atomic_ir(llvm_ir: str) -> None:
    patterns = (
        r"atomicrmw add ptr %slot_val, i8 %\d+ monotonic, align 1",
        r"atomicrmw sub ptr %slot_val, i8 %\d+ seq_cst, align 1",
        r"atomicrmw add ptr %slot_val, i16 %\d+ monotonic, align 2",
        r"atomicrmw sub ptr %slot_val, i16 %\d+ seq_cst, align 2",
        r"atomicrmw add ptr %slot_val, i32 %\d+ acquire, align 4",
        r"atomicrmw sub ptr %slot_val, i32 %\d+ release, align 4",
        r"atomicrmw add ptr %value_val, i64 %\d+ acq_rel, align 8",
        r"atomicrmw add ptr %slot_val, i64 %\d+ seq_cst, align 8",
        r"load atomic i64, ptr %value_val acquire, align 8",
        r"store atomic i8 %atomic_bool_store, ptr %ready_val release, align 1",
        r"load atomic i8, ptr %ready_val acquire, align 1",
        r"atomicrmw xchg ptr %slot_val, i8 %atomic_bool_store seq_cst, align 1",
        r"atomicrmw xchg ptr %slot_val, float %value_val seq_cst, align 4",
        r"store atomic double %value_val, ptr %slot_val monotonic, align 8",
        r"atomicrmw xchg ptr %slot_val, ptr %value_val seq_cst, align 8",
    )
    for pattern in patterns:
        if not re.search(pattern, llvm_ir):
            raise RuntimeError(f"missing atomic LLVM instruction {pattern!r}:\n{llvm_ir}")


def make_c_caller(symbols: dict[str, str]) -> str:
    return f'''\
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
extern uint64_t increment(void *) __asm__("{symbols['increment']}");
extern uint64_t load_count(void *) __asm__("{symbols['load count']}");
extern void publish(void *) __asm__("{symbols['publish']}");
extern bool is_ready(void *) __asm__("{symbols['is ready']}");
extern bool exchange_boolean(void *, bool) __asm__("{symbols['exchange boolean']}");
extern int8_t add_signed_octet(void *) __asm__("{symbols['add signed octet']}");
extern uint8_t subtract_unsigned_octet(void *) __asm__("{symbols['subtract unsigned octet']}");
extern float exchange_float(void *, float) __asm__("{symbols['exchange float']}");
extern void *exchange_pointer(void *, void *) __asm__("{symbols['exchange pointer']}");
extern uint32_t wrapped_exchange(void *, uint32_t) __asm__("{symbols['wrapped exchange']}");
extern uint32_t wrapped_increment(void *) __asm__("{symbols['wrapped increment']}");
extern uint32_t wrapped_load(void *) __asm__("{symbols['wrapped load']}");
extern void wrapped_store(void *, uint32_t) __asm__("{symbols['wrapped store']}");
static _Atomic uint64_t count;
static _Atomic bool flag;
static int payload;
static float float_from_bits(uint32_t bits) {{
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}}
static uint32_t float_bits(float value) {{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}}
static void *worker(void *unused) {{
    (void)unused;
    for (int index = 0; index < 10000; ++index)
        increment(&count);
    return 0;
}}
static void *consumer(void *unused) {{
    (void)unused;
    while (!is_ready(&flag)) {{}}
    return payload == 42 ? 0 : (void *)1;
}}
int main(void) {{
    pthread_t workers[4], reader;
    for (int index = 0; index < 4; ++index)
        if (pthread_create(&workers[index], 0, worker, 0)) return 1;
    for (int index = 0; index < 4; ++index)
        if (pthread_join(workers[index], 0)) return 2;
    if (load_count(&count) != 40000) return 3;
    if (pthread_create(&reader, 0, consumer, 0)) return 4;
    payload = 42;
    publish(&flag);
    void *result = 0;
    if (pthread_join(reader, &result) || result) return 5;
    _Atomic bool boolean = true;
    if (!exchange_boolean(&boolean, false) || atomic_load(&boolean)) return 6;
    _Atomic int8_t signed_octet = INT8_MAX;
    if (add_signed_octet(&signed_octet) != INT8_MAX || atomic_load(&signed_octet) != INT8_MIN) return 7;
    _Atomic uint8_t unsigned_octet = 0;
    if (subtract_unsigned_octet(&unsigned_octet) != 0 || atomic_load(&unsigned_octet) != UINT8_MAX) return 8;
    _Atomic float floating = 0;
    atomic_store(&floating, float_from_bits(UINT32_C(0x7fc01234)));
    if (float_bits(exchange_float(&floating, float_from_bits(UINT32_C(0x3f800001)))) != UINT32_C(0x7fc01234)) return 9;
    if (float_bits(atomic_load(&floating)) != UINT32_C(0x3f800001)) return 10;
    _Atomic(void *) pointer = (void *)(uintptr_t)UINT32_C(0x1234);
    void *replacement = (void *)(uintptr_t)UINT32_C(0x5678);
    if (exchange_pointer(&pointer, replacement) != (void *)(uintptr_t)UINT32_C(0x1234)) return 11;
    if (atomic_load(&pointer) != replacement) return 12;
    _Atomic uint32_t wrapped = 7;
    if (wrapped_exchange(&wrapped, 9) != 7 || wrapped_increment(&wrapped) != 9) return 13;
    if (wrapped_load(&wrapped) != 10) return 14;
    wrapped_store(&wrapped, 11);
    return atomic_load(&wrapped) == 11 ? 0 : 15;
}}
'''


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_native_atomics.py <compiler>", file=sys.stderr)
        return 2
    compiler = Path(sys.argv[1]).resolve()
    if not compiler.is_file():
        print(f"compiler not found: {compiler}", file=sys.stderr)
        return 2
    repo_root = Path(__file__).resolve().parent.parent
    c_compiler = os.environ.get("CC", "cc")
    try:
        with tempfile.TemporaryDirectory(prefix="dynlex-atomics-") as temporary_directory:
            temporary = Path(temporary_directory)
            library = temporary / "atomics.dl"
            library.write_text(LIBRARY_SOURCE, encoding="utf-8")
            llvm_output = temporary / "atomics.ll"
            require_success(
                run([str(compiler), str(library), "--emit-llvm", "--no-main", "-o", str(llvm_output)], repo_root)
            )
            llvm_ir = llvm_output.read_text(encoding="utf-8")
            require_atomic_ir(llvm_ir)
            names = (
                "increment", "load count", "publish", "is ready", "exchange boolean", "add signed octet",
                "subtract unsigned octet", "exchange float", "exchange pointer", "wrapped exchange",
                "wrapped increment", "wrapped load", "wrapped store",
            )
            symbols = {name: exposed_symbol(llvm_ir, name) for name in names}
            caller = temporary / "caller.c"
            caller.write_text(make_c_caller(symbols), encoding="utf-8")
            object_output = temporary / "atomics.o"
            executable = temporary / "caller"
            for optimization in ("-O0", "-O2", "-O3"):
                require_success(
                    run(
                        [str(compiler), str(library), "--emit-object", "--no-main", optimization, "-o", str(object_output)],
                        repo_root,
                    )
                )
                require_success(
                    run(
                        [c_compiler, "-std=c11", "-O2", "-pthread", str(caller), str(object_output), "-o", str(executable)],
                        repo_root,
                    )
                )
                require_success(run([str(executable)], repo_root))
            state_source = temporary / "state.dl"
            state_source.write_text(STATE_TRACKING_SOURCE, encoding="utf-8")
            state_executable = temporary / "state"
            require_success(run([str(compiler), str(state_source), "-o", str(state_executable)], repo_root))
            state_result = run([str(state_executable)], repo_root)
            require_success(state_result)
            if state_result.stdout != "2\n":
                raise RuntimeError(f"atomic fetch add left stale inferred state: {state_result.stdout!r}")
            for name, (source, expected) in NEGATIVE_SOURCES.items():
                negative = temporary / f"negative-{name}.dl"
                negative.write_text("import lib/std.dl\n" + source, encoding="utf-8")
                require_failure(run([str(compiler), str(negative), "--emit-llvm"], repo_root), expected)
            target = temporary / "target.dl"
            target.write_text(
                'import lib/std.dl\nset value to 0\n@intrinsic("atomic load", the address of value, "relaxed")\n',
                encoding="utf-8",
            )
            for arguments in (("--emit-wasm",), ("--emit-spirv", "--shader-stage=fragment")):
                require_failure(
                    run([str(compiler), str(target), *arguments], repo_root),
                    "Atomic operations are only available when emitting native CPU code",
                )
    except RuntimeError as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
