#!/usr/bin/env python3
"""Type errors at callback boundaries must be diagnostics, not invalid native calls."""
import argparse
from pathlib import Path
import subprocess
import tempfile


PREFIX = '''import lib/callbacks.dl
this argument means: @intrinsic("receiver")
section responder {name}:
    declares: name
    receiver: integer
    returns: integer
    value: @intrinsic("deferred body")
'''

CASES = {
    "return_mismatch": (PREFIX + '''responder bad:
    return 1.5
ignore bad
''', "Callback return type"),
    "argument_mismatch": (PREFIX + '''responder identity:
    return this argument
ignore @intrinsic("invoke", identity, "wrong")
''', "Callback argument"),
    "argument_count": (PREFIX + '''responder identity:
    return this argument
ignore @intrinsic("invoke", identity)
''', "Callback expects"),
    "raw_pointer": ('''import lib/callbacks.dl
ignore @intrinsic("invoke", 0 as a pointer)
''', "typed callback"),
    "declared_abi_mismatch": (PREFIX + '''responder identity:
    return this argument
ignore @intrinsic("call pointer", identity, a floating-point number, 1)
''', "Callback return type"),
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("project", type=Path)
    parser.add_argument("compiler", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="dynlex-typed-callback-") as directory:
        for name, (source, message) in CASES.items():
            path = Path(directory) / f"{name}.dl"
            path.write_text(source)
            result = subprocess.run([str(args.compiler.resolve()), str(path), "-o", str(Path(directory) / "case.out")], cwd=args.project.resolve(), text=True, capture_output=True, timeout=15)
            diagnostics = result.stdout + result.stderr
            if result.returncode != 1 or message not in diagnostics or "Error:" not in diagnostics:
                raise RuntimeError(f"{name}: expected a {message!r} diagnostic; got exit={result.returncode}, {diagnostics}")
    print("Typed callback argument and result diagnostics passed")


if __name__ == "__main__":
    main()
