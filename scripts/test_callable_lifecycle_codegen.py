#!/usr/bin/env python3

import pathlib
import re
import subprocess
import sys
import tempfile


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: test_callable_lifecycle_codegen.py <compiler> <source>", file=sys.stderr)
        return 2

    compiler = pathlib.Path(sys.argv[1]).resolve()
    source = pathlib.Path(sys.argv[2]).resolve()
    with tempfile.TemporaryDirectory() as temporary_directory:
        llvm_path = pathlib.Path(temporary_directory) / "callable_lifecycle.ll"
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

    wrapper_match = re.search(
        r'^define [^{@]*@(?P<name>[^\s(]*replace[^\s(]*_callable[^\s(]*)\([^\n]*\) \{\n(?P<body>.*?)^\}',
        llvm,
        flags=re.MULTILINE | re.DOTALL,
    )
    if not wrapper_match:
        print("managed callable wrapper was not emitted", file=sys.stderr)
        return 1
    body = wrapper_match.group("body")
    retain_calls = [match.start() for match in re.finditer(r'call void @[^\s(]*retain', body)]
    release_calls = [match.start() for match in re.finditer(r'call void @[^\s(]*release', body)]
    wrapped_calls = [match.start() for match in re.finditer(r'call void @[^\s(]*replace(?![^\s(]*_callable)', body)]
    if len(retain_calls) != 1 or len(release_calls) != 1 or len(wrapped_calls) != 1:
        print(
            "managed callable wrapper must retain its borrowed input, call the implementation, and release its final value "
            f"exactly once; found {len(retain_calls)} retains, {len(wrapped_calls)} calls, and {len(release_calls)} releases",
            file=sys.stderr,
        )
        return 1
    if not retain_calls[0] < wrapped_calls[0] < release_calls[0]:
        print("managed callable wrapper lifecycle calls are in the wrong order", file=sys.stderr)
        return 1
    if not re.search(r'^\s+%managed_initialized\d* = alloca i1', body, flags=re.MULTILINE):
        print("managed callable wrapper has no initialization state for its parameter storage", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
