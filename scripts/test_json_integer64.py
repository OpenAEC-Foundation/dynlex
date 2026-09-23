#!/usr/bin/env python3
"""Verify lossless JSON integer construction and reads through the native C ABI."""

from __future__ import annotations

import os
import re
import signal
import subprocess
import sys
import tempfile
from pathlib import Path

from native_test_support import resolve_c_compiler
from process_error_mode import unattended_child_processes

if os.name == "posix":
    import resource


LIBRARY_SOURCE = """\
import json.dl

exposed function signed JSON round trip {a 64 bit integer:value}:
    execute:
        set node to a new JSON number from value
        set result to the signed 64 bit JSON integer read from node
        set output to the signed 64 bit integer value of result
        free node
        return output

exposed function unsigned JSON round trip {a 64 bit unsigned integer:value}:
    execute:
        set node to a new JSON number from value
        set result to the unsigned 64 bit JSON integer read from node
        set output to the unsigned 64 bit integer value of result
        free node
        return output
"""

FAILED_SIGNED_GETTER_SOURCE = """\
import json.dl

parse "1.5" as JSON and set parsed to it
set outcome to the signed 64 bit JSON integer read from the value of parsed
print the signed 64 bit integer value of outcome as a line
"""

FAILED_UNSIGNED_GETTER_SOURCE = """\
import json.dl

parse "1.5" as JSON and set parsed to it
set outcome to the unsigned 64 bit JSON integer read from the value of parsed
print the unsigned 64 bit integer value of outcome as a line
"""


def disable_core_dumps() -> None:
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def run(
    arguments: list[str], working_directory: Path, *, disable_core_dump: bool = False
) -> subprocess.CompletedProcess[str]:
    options = {}
    if disable_core_dump and os.name == "posix":
        options["preexec_fn"] = disable_core_dumps
    with unattended_child_processes():
        return subprocess.run(
            arguments,
            cwd=working_directory,
            text=True,
            capture_output=True,
            check=False,
            timeout=60,
            **options,
        )


def require_success(result: subprocess.CompletedProcess[str]) -> None:
    if result.returncode != 0:
        raise RuntimeError(f"command {result.args} exited {result.returncode}:\n{result.stdout}{result.stderr}")


def require_abort(result: subprocess.CompletedProcess[str]) -> None:
    expected_return_codes = (134, 0xC0000409) if os.name == "nt" else (-signal.SIGABRT,)
    if result.returncode not in expected_return_codes:
        raise RuntimeError(
            f"command {result.args} exited {result.returncode}, expected abort {expected_return_codes}:\n"
            f"{result.stdout}{result.stderr}"
        )


def exposed_symbol(llvm_ir: str, function_name: str) -> str:
    match = re.search(
        rf"^define .* @([^( ]*{function_name.replace(' ', '_')}[^ (]*_callable[^ (]*)\(",
        llvm_ir,
        re.MULTILINE,
    )
    if not match:
        raise RuntimeError(f"LLVM output omitted exposed function {function_name!r}:\n{llvm_ir}")
    return match.group(1)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_json_integer64.py <compiler>", file=sys.stderr)
        return 2
    compiler = Path(sys.argv[1]).resolve()
    if not compiler.is_file():
        print(f"compiler not found: {compiler}", file=sys.stderr)
        return 2
    repo_root = Path(__file__).resolve().parent.parent
    c_compiler = resolve_c_compiler(compiler)
    executable_suffix = ".exe" if os.name == "nt" else ""
    try:
        with tempfile.TemporaryDirectory(prefix="dynlex-json-integer64-") as temporary_directory:
            temporary = Path(temporary_directory)
            library = temporary / "json_integer64.dl"
            library.write_text(LIBRARY_SOURCE, encoding="utf-8")
            llvm_output = temporary / "json_integer64.ll"
            require_success(
                run([str(compiler), str(library), "--emit-llvm", "--no-main", "-o", str(llvm_output)], repo_root)
            )
            llvm_ir = llvm_output.read_text(encoding="utf-8")
            signed_symbol = exposed_symbol(llvm_ir, "signed JSON round trip")
            unsigned_symbol = exposed_symbol(llvm_ir, "unsigned JSON round trip")
            caller = temporary / "caller.c"
            caller.write_text(
                f"""\
#include <stdint.h>
extern int64_t signed_round_trip(int64_t) __asm__("{signed_symbol}");
extern uint64_t unsigned_round_trip(uint64_t) __asm__("{unsigned_symbol}");
int main(void) {{
    if (signed_round_trip(INT64_MIN) != INT64_MIN) return 1;
    if (signed_round_trip(INT64_MAX) != INT64_MAX) return 2;
    if (unsigned_round_trip(UINT64_C(9007199254740993)) != UINT64_C(9007199254740993)) return 3;
    if (unsigned_round_trip(UINT64_MAX) != UINT64_MAX) return 4;
    return 0;
}}
""",
                encoding="utf-8",
            )
            object_output = temporary / "json_integer64.o"
            executable = temporary / f"caller{executable_suffix}"
            for optimization in ("-O0", "-O2", "-O3"):
                require_success(
                    run(
                        [str(compiler), str(library), "--emit-object", "--no-main", optimization, "-o", str(object_output)],
                        repo_root,
                    )
                )
                require_success(
                    run([c_compiler, optimization, str(caller), str(object_output), "-o", str(executable)], repo_root)
                )
                require_success(run([str(executable)], repo_root))

            for name, source in (
                ("failed_signed_getter", FAILED_SIGNED_GETTER_SOURCE),
                ("failed_unsigned_getter", FAILED_UNSIGNED_GETTER_SOURCE),
            ):
                failed_getter_source = temporary / f"{name}.dl"
                failed_getter_source.write_text(source, encoding="utf-8")
                failed_getter = temporary / f"{name}{executable_suffix}"
                require_success(run([str(compiler), str(failed_getter_source), "-o", str(failed_getter)], repo_root))
                require_abort(run([str(failed_getter)], repo_root, disable_core_dump=True))
    except (RuntimeError, OSError, subprocess.TimeoutExpired) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
