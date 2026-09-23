#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare path-sweep anchors and placements with the pinned source."""
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
POINT_CASES = [
    "line anchor", "reversed connected chain", "circle centre", "arc midpoint",
    "multiple wires", "group anchor", "ellipse centre", "clockwise bulge",
    "scaled-plane arc", "spline profile", "unbounded profile", "empty profile", "planar path start",
    "spatial path start", "stationary path", "reversed planar path",
    "spline path start", "disconnected path", "planar arc path start",
]
PLACEMENT_CASES = [
    "aligned placement", "unaligned quarter turn", "axis-parallel placement",
    "reversed axis-parallel placement", "explicit base point",
    "spline placement", "deformation-independent placement",
]
FIELD_COUNT = len(POINT_CASES) * 4 + len(PLACEMENT_CASES) * 13


def process(command: list[str | Path], timeout: float = 180) -> str:
    status, output, _ = fixtures.run_process(
        [str(part) for part in command], timeout=timeout, cwd=ROOT,
    )
    if status:
        raise AssertionError(f"exit {status}: {command}\n{output}")
    return output.strip()


def numeric_output(binary: Path) -> tuple[list[float], str]:
    output = process([binary], timeout=30)
    values = [float(token) for token in output.split()]
    if len(values) != FIELD_COUNT or not all(math.isfinite(v) for v in values):
        raise AssertionError(f"unexpected output from {binary}: {len(values)} fields")
    return values, output


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--reference-root", type=Path,
                        default=ROOT / "build/cadkernel-upstream-offset-reference")
    parser.add_argument("--compiler", type=Path, default=ROOT / "build/dynlex.exe")
    parser.add_argument("--rustc", type=Path, default=Path("rustc"))
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    source = args.source.resolve()
    revision = process(["git", "-c", f"safe.directory={source.as_posix()}",
                        "-C", source, "rev-parse", "HEAD"])
    if revision != PIN:
        raise AssertionError(f"source revision {revision} != {PIN}")
    if hashlib.sha256((source / "src/brep/sweep_path.rs").read_bytes()).hexdigest() != SOURCE_HASH:
        raise AssertionError("pinned path-sweep source changed")
    compiler_hash = hashlib.sha256(args.compiler.read_bytes()).hexdigest()
    output = args.output or Path(tempfile.mkdtemp(prefix="brep-path-base-diff-", dir=ROOT / "build"))
    output.mkdir(parents=True, exist_ok=True)
    native_text: dict[str, str] = {}
    for mode, directory in (("O0", "debug"), ("O2", "release")):
        library = args.reference_root / directory / "libcadkernel.rlib"
        dependencies = args.reference_root / directory / "deps"
        if not library.is_file() or not dependencies.is_dir():
            raise AssertionError(f"missing pinned reference build: {library}")
        reference = output / f"reference-{mode}.exe"
        native = output / f"native-{mode}.exe"
        process([args.rustc, "--edition=2021", HERE / "reference.rs", "--extern",
                 f"cadkernel={library.resolve()}", "-L", f"dependency={dependencies.resolve()}",
                 "-C", f"opt-level={mode[1]}", "-o", reference])
        process([args.compiler.resolve(), HERE / "probe.dl", f"-{mode}", "-o", native])
        expected, _ = numeric_output(reference)
        actual, native_text[mode] = numeric_output(native)
        for index, (source_value, native_value) in enumerate(zip(expected, actual)):
            if not math.isclose(native_value, source_value, rel_tol=1e-10, abs_tol=1e-10):
                if index < len(POINT_CASES) * 4:
                    case = POINT_CASES[index // 4]
                else:
                    case = PLACEMENT_CASES[(index - len(POINT_CASES) * 4) // 13]
                raise AssertionError(
                    f"{mode} {case} field {index}: DynLex={native_value}, source={source_value}"
                )
        print(f"{mode}: {len(POINT_CASES) + len(PLACEMENT_CASES)} cases, {FIELD_COUNT} fields match", flush=True)
    if native_text["O0"] != native_text["O2"]:
        raise AssertionError("native optimization modes diverged")
    if hashlib.sha256(args.compiler.read_bytes()).hexdigest() != compiler_hash:
        raise AssertionError("compiler changed during verification")
    print(f"{2 * (len(POINT_CASES) + len(PLACEMENT_CASES))} case/mode runs; "
          f"{2 * FIELD_COUNT} comparisons; exact native optimization parity", flush=True)


if __name__ == "__main__":
    main()
