#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare native tapered sheets with the pinned source kernel."""
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
SWEEP_HASH = "492ac57683c6681f6e84f9fbbbdee2001b2b18a2bd766eb714588f90e4ffb995"


def process(command: list[str | Path], *, timeout: float = 180) -> str:
    status, output, _ = fixtures.run_process(
        [str(part) for part in command], timeout=timeout, cwd=ROOT,
    )
    if status:
        raise AssertionError(f"exit {status}: {command}\n{output}")
    return output.strip()


def numeric_output(binary: Path) -> tuple[list[float], str]:
    output = process([binary], timeout=30)
    values = [float(token) for token in output.split()]
    if len(values) != 337 or not all(math.isfinite(value) for value in values):
        raise AssertionError(f"unexpected probe output from {binary}: {len(values)} fields")
    return values, output


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="pinned source checkout")
    parser.add_argument("--reference-root", type=Path, default=ROOT / "build/cadkernel-upstream-offset-reference")
    parser.add_argument("--compiler", type=Path, default=ROOT / "build/dynlex.exe")
    parser.add_argument("--rustc", type=Path, default=Path("rustc"))
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    source = args.source.resolve()
    revision = process(["git", "-c", f"safe.directory={source.as_posix()}",
                        "-C", source, "rev-parse", "HEAD"])
    if revision != PIN:
        raise AssertionError(f"source revision {revision} != {PIN}")
    source_file = source / "src/brep/sweep.rs"
    if hashlib.sha256(source_file.read_bytes()).hexdigest() != SWEEP_HASH:
        raise AssertionError("pinned sweep source changed")
    compiler_hash = hashlib.sha256(args.compiler.read_bytes()).hexdigest()

    output = args.output or Path(tempfile.mkdtemp(prefix="brep-taper-diff-", dir=ROOT / "build"))
    output.mkdir(parents=True, exist_ok=True)
    results: dict[tuple[str, str], list[float]] = {}
    native_text: dict[str, str] = {}
    for mode, directory in (("O0", "debug"), ("O2", "release")):
        reference_library = args.reference_root / directory / "libcadkernel.rlib"
        dependencies = args.reference_root / directory / "deps"
        if not reference_library.is_file() or not dependencies.is_dir():
            raise AssertionError(f"missing reference build with brep,offset features: {reference_library}")
        reference = output / f"reference-{mode}.exe"
        native = output / f"native-{mode}.exe"
        process([
            args.rustc, "--edition=2021", HERE / "reference.rs",
            "--extern", f"cadkernel={reference_library.resolve()}",
            "-L", f"dependency={dependencies.resolve()}",
            "-C", f"opt-level={mode[1]}", "-o", reference,
        ])
        process([args.compiler.resolve(), HERE / "probe.dl", f"-{mode}", "-o", native])
        results[(mode, "Rust")], _ = numeric_output(reference)
        results[(mode, "DynLex")], native_text[mode] = numeric_output(native)
        for field, (expected, actual) in enumerate(zip(results[(mode, "Rust")], results[(mode, "DynLex")])):
            if not math.isclose(actual, expected, rel_tol=1e-10, abs_tol=1e-10):
                raise AssertionError(f"{mode} field {field}: DynLex={actual}, source={expected}")
        print(f"{mode}: 3 cases, {len(results[(mode, 'Rust')])} fields match", flush=True)
    if native_text["O0"] != native_text["O2"]:
        raise AssertionError("native optimization modes diverged")
    if hashlib.sha256(args.compiler.read_bytes()).hexdigest() != compiler_hash:
        raise AssertionError("compiler changed during verification")
    print("6 case/mode runs; 674 comparisons; exact native optimization parity", flush=True)


if __name__ == "__main__":
    main()
