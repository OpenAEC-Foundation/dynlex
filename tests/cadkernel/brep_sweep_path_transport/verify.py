#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare native path transport with pinned Rust formulas at O0 and O2."""
from __future__ import annotations

import argparse
import hashlib
import math
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "tests/cadkernel"))
import verify as fixtures

PIN = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"
SOURCE_HASH = "a67183c89426b99608a59fd02a600aab4e5b415b00d5767898a6812766078fef"
FIELD_COUNT = 56


def run(command: list[str | Path], timeout: float = 90) -> str:
    status, output, _ = fixtures.run_process([str(part) for part in command], timeout=timeout, cwd=ROOT)
    if status:
        raise AssertionError(f"exit {status}: {command}\n{output}")
    return output.strip()


def values(label: str, output: str) -> list[float]:
    result = [float(token) for token in output.split()]
    if len(result) != FIELD_COUNT or not all(math.isfinite(item) for item in result):
        raise AssertionError(f"{label}: expected {FIELD_COUNT} finite fields, got {len(result)}")
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--reference-root", type=Path,
                        default=ROOT / "build/cadkernel-upstream-offset-reference")
    parser.add_argument("--compiler", type=Path, default=ROOT / "build/dynlex.exe")
    parser.add_argument("--rustc", type=Path, default=Path("rustc"))
    arguments = parser.parse_args()
    source = arguments.source.resolve()
    revision = run(["git", "-c", f"safe.directory={source.as_posix()}",
                    "-C", source, "rev-parse", "HEAD"])
    if revision != PIN:
        raise AssertionError(f"source revision {revision} != {PIN}")
    digest = hashlib.sha256((source / "src/brep/sweep_path.rs").read_bytes()).hexdigest()
    if digest != SOURCE_HASH:
        raise AssertionError("pinned transport source changed")
    with tempfile.TemporaryDirectory(prefix="cad-path-transport-") as temporary:
        output = Path(temporary)
        parity: list[str] = []
        for mode, directory in (("O0", "debug"), ("O2", "release")):
            library = (arguments.reference_root / directory / "libcadkernel.rlib").resolve()
            dependencies = (arguments.reference_root / directory / "deps").resolve()
            if not library.is_file() or not dependencies.is_dir():
                raise AssertionError(f"missing pinned Rust library: {library}")
            rust_binary = output / f"reference-{mode}.exe"
            native_binary = output / f"native-{mode}.exe"
            run([arguments.rustc, "--edition=2021", HERE / "reference.rs", "--extern",
                 f"cadkernel={library}", "-L", f"dependency={dependencies}",
                 "-C", f"opt-level={mode[1]}", "-o", rust_binary])
            diagnostic = run([arguments.compiler, HERE / "probe.dl", f"-{mode}", "-o", native_binary])
            if diagnostic:
                raise AssertionError(f"DynLex diagnostic {mode}: {diagnostic}")
            expected = values(f"Rust/{mode}", run([rust_binary]))
            actual_text = run([native_binary])
            actual = values(f"DynLex/{mode}", actual_text)
            for index, (want, got) in enumerate(zip(expected, actual)):
                if not math.isclose(want, got, rel_tol=2e-12, abs_tol=2e-12):
                    raise AssertionError(f"{mode} field {index}: DynLex {got} != Rust {want}")
            parity.append(actual_text)
            print(f"PASS {mode}: 11 cases / {FIELD_COUNT} fields", flush=True)
        if parity[0] != parity[1]:
            raise AssertionError("native O0/O2 output differs")
    print("PASS: native optimization parity", flush=True)


if __name__ == "__main__":
    main()
