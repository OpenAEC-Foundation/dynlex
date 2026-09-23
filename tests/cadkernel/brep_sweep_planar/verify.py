#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare native planar path sweeps with the pinned Rust implementation."""
from __future__ import annotations

import argparse
import hashlib
import json
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
EXPECTED_VALID = (1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 0)


def process(command: list[str | Path], *, timeout: float = 240) -> str:
    status, output, _ = fixtures.run_process(
        [str(part) for part in command], timeout=timeout, cwd=ROOT,
    )
    if status:
        raise AssertionError(f"exit {status}: {command}\n{output}")
    return output.strip()


def values(binary: Path, case: int) -> tuple[list[float], str]:
    output = process([binary, str(case)], timeout=30)
    try:
        numbers = [float(token) for token in output.split()]
    except ValueError as error:
        raise AssertionError(f"non-numeric output: {binary}, case {case}: {output!r}") from error
    if not numbers or not all(map(math.isfinite, numbers)):
        raise AssertionError(f"invalid output: {binary}, case {case}: {output!r}")
    if numbers[0] != EXPECTED_VALID[case]:
        raise AssertionError(f"case {case}: {binary} returned validity {numbers[0]}")
    if numbers[0] == 0 and len(numbers) != 1:
        raise AssertionError(f"case {case}: invalid body has extra fields")
    if numbers[0] == 1:
        if len(numbers) < 12 or numbers[11] != 0:
            raise AssertionError(f"case {case}: malformed body or validation flaws")
        if len(numbers) != 12 + 3 * int(numbers[1]):
            raise AssertionError(f"case {case}: vertex count disagrees with coordinates")
    return numbers, output


def compare(label: str, source: list[float], native: list[float]) -> int:
    if len(source) != len(native):
        raise AssertionError(f"{label}: {len(source)} source fields versus {len(native)} native fields")
    for index, (expected, actual) in enumerate(zip(source, native)):
        if not math.isclose(actual, expected, rel_tol=1e-10, abs_tol=1e-10):
            raise AssertionError(f"{label} field {index}: native={actual}, Rust={expected}")
    return len(source)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--reference-root", type=Path, default=ROOT / "build/cadkernel-upstream-offset-reference")
    parser.add_argument("--compiler", type=Path, default=ROOT / "build/dynlex.exe")
    parser.add_argument("--rustc", type=Path, default=Path("rustc"))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()

    source = args.source.resolve()
    revision = process(["git", "-c", f"safe.directory={source.as_posix()}",
                        "-C", source, "rev-parse", "HEAD"])
    if revision != PIN:
        raise AssertionError(f"source revision {revision} != {PIN}")
    if hashlib.sha256((source / "src/brep/sweep.rs").read_bytes()).hexdigest() != SWEEP_HASH:
        raise AssertionError("pinned sweep source changed")
    compiler_hash = hashlib.sha256(args.compiler.read_bytes()).hexdigest()

    output = args.output or Path(tempfile.mkdtemp(prefix="brep-planar-path-diff-", dir=ROOT / "build"))
    output.mkdir(parents=True, exist_ok=True)
    suffix = ".exe" if sys.platform == "win32" else ".out"
    binaries: dict[str, dict[str, Path]] = {}
    for mode, directory in (("O0", "debug"), ("O2", "release")):
        library = args.reference_root / directory / "libcadkernel.rlib"
        dependencies = args.reference_root / directory / "deps"
        if not library.is_file() or not dependencies.is_dir():
            raise AssertionError(f"missing pinned reference library: {library}")
        reference = output / f"reference-{mode}{suffix}"
        native = output / f"native-{mode}{suffix}"
        if not args.skip_build:
            process([args.rustc, "--edition=2021", HERE / "reference.rs",
                     "--extern", f"cadkernel={library.resolve()}",
                     "-L", f"dependency={dependencies.resolve()}",
                     "-C", f"opt-level={mode[1]}", "-o", reference])
            diagnostics = process([args.compiler.resolve(), HERE / "probe.dl", f"-{mode}", "-o", native])
            if diagnostics:
                raise AssertionError(f"{mode}: compiler diagnostics\n{diagnostics}")
        if not reference.is_file() or not native.is_file():
            raise AssertionError(f"missing {mode} binaries")
        binaries[mode] = {"Rust": reference, "DynLex": native}
        print(f"{mode}: probe binaries ready", flush=True)

    comparisons = 0
    for case in range(len(EXPECTED_VALID)):
        results: dict[tuple[str, str], list[float]] = {}
        outputs: dict[tuple[str, str], str] = {}
        for mode in ("O0", "O2"):
            for language in ("Rust", "DynLex"):
                results[(mode, language)], outputs[(mode, language)] = values(binaries[mode][language], case)
            comparisons += compare(f"case {case}/{mode}", results[(mode, "Rust")], results[(mode, "DynLex")])
        if outputs[("O0", "DynLex")] != outputs[("O2", "DynLex")]:
            raise AssertionError(f"case {case}: native O0/O2 output differs")
        compare(f"case {case}/Rust O0/O2", results[("O0", "Rust")], results[("O2", "Rust")])
        print(f"PASS case {case}: {len(results[('O0', 'Rust')])} fields per mode", flush=True)

    if hashlib.sha256(args.compiler.read_bytes()).hexdigest() != compiler_hash:
        raise AssertionError("compiler changed during verification")
    summary = {
        "revision": PIN,
        "source_sha256": SWEEP_HASH,
        "compiler_sha256": compiler_hash,
        "cases": len(EXPECTED_VALID),
        "field_comparisons": comparisons,
        "native_exact_optimization_parity": True,
        "reference_library_sha256": {
            mode: hashlib.sha256((args.reference_root / directory / "libcadkernel.rlib").read_bytes()).hexdigest()
            for mode, directory in (("O0", "debug"), ("O2", "release"))
        },
    }
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: {len(EXPECTED_VALID)} cases, {comparisons} field comparisons; artifacts={output}", flush=True)


if __name__ == "__main__":
    main()
