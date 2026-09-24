#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare prepared native sheet assembly with the pinned public Rust path sweep."""
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
CASE_FIELDS = 22
CASES = ("one-band", "two-band", "reverse-orientation", "rational-profile")


def run(command: list[str | Path], *, timeout: float = 180) -> str:
    status, output, _ = fixtures.run_process([str(item) for item in command], timeout=timeout, cwd=ROOT)
    if status:
        raise AssertionError(f"exit {status}: {command}\n{output}")
    return output.strip()


def samples(binary: Path) -> list[list[float]]:
    values = [float(token) for token in run([binary], timeout=30).split()]
    if len(values) != len(CASES) * CASE_FIELDS or not all(math.isfinite(value) for value in values):
        raise AssertionError(f"unexpected probe output from {binary}: {len(values)} fields")
    return [values[offset:offset + CASE_FIELDS] for offset in range(0, len(values), CASE_FIELDS)]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--reference-root", type=Path, default=ROOT / "build/cadkernel-upstream-offset-reference")
    parser.add_argument("--compiler", type=Path, default=ROOT / "build/dynlex.exe")
    parser.add_argument("--rustc", type=Path, default=Path("rustc"))
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    source = args.source.resolve()
    revision = run(["git", "-c", f"safe.directory={source.as_posix()}", "-C", source, "rev-parse", "HEAD"])
    if revision != PIN:
        raise AssertionError(f"source revision {revision} != {PIN}")
    source_file = source / "src/brep/sweep_path.rs"
    if hashlib.sha256(source_file.read_bytes()).hexdigest() != SOURCE_HASH:
        raise AssertionError("pinned path sweep source changed")
    compiler = args.compiler.resolve()
    compiler_hash = hashlib.sha256(compiler.read_bytes()).hexdigest()
    output = args.output or Path(tempfile.mkdtemp(prefix="brep-path-body-diff-", dir=ROOT / "build"))
    output.mkdir(parents=True, exist_ok=True)
    native_by_mode: dict[str, list[list[float]]] = {}

    for mode, directory in (("O0", "debug"), ("O2", "release")):
        reference_library = args.reference_root / directory / "libcadkernel.rlib"
        dependencies = args.reference_root / directory / "deps"
        if not reference_library.is_file() or not dependencies.is_dir():
            raise AssertionError(f"missing reference build: {reference_library}")
        reference = output / f"reference-{mode}.exe"
        native = output / f"native-{mode}.exe"
        run([args.rustc, "--edition=2021", HERE / "reference.rs", "--extern",
             f"cadkernel={reference_library.resolve()}", "-L", f"dependency={dependencies.resolve()}",
             "-C", f"opt-level={mode[1]}", "-o", reference])
        run([compiler, HERE / "probe.dl", f"-{mode}", "-o", native])
        expected = samples(reference)
        actual = samples(native)
        for case, left, right in zip(CASES, expected, actual):
            for field, (rust, dynlex) in enumerate(zip(left, right)):
                if not math.isclose(rust, dynlex, rel_tol=1e-10, abs_tol=1e-10):
                    raise AssertionError(f"{mode} {case} field {field}: Rust={rust}, DynLex={dynlex}")
        native_by_mode[mode] = actual
        print(f"{mode}: {len(CASES)} cases, {len(CASES) * CASE_FIELDS} fields match", flush=True)

    for case, left, right in zip(CASES, native_by_mode["O0"], native_by_mode["O2"]):
        for field, (zero, two) in enumerate(zip(left, right)):
            if not math.isclose(zero, two, rel_tol=1e-12, abs_tol=1e-12):
                raise AssertionError(f"optimization mismatch {case} field {field}: {zero} != {two}")
    if hashlib.sha256(compiler.read_bytes()).hexdigest() != compiler_hash:
        raise AssertionError("compiler changed during verification")
    print(f"compiler SHA256 {compiler_hash}; {2 * len(CASES) * CASE_FIELDS} comparisons", flush=True)


if __name__ == "__main__":
    main()
