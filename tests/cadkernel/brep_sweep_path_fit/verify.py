#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare the cubic frame fitter and sampled regularity with pinned Rust."""
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
CASES = (
    "linear-forward", "quartic-reverse", "refused-fold", "spatial-forward",
    "tight-fit-empty-profile", "degenerate-profile", "mixed-orientation",
)


def run(command: list[str | Path], timeout: float = 180) -> str:
    status, output, _ = fixtures.run_process([str(part) for part in command], timeout=timeout, cwd=ROOT)
    if status:
        raise AssertionError(f"exit {status}: {command}\n{output}")
    return output.strip()


def numbers(label: str, output: str) -> list[float]:
    values = [float(token) for token in output.split()]
    if len(values) < 4 or not all(map(math.isfinite, values)):
        raise AssertionError(f"{label}: invalid numeric output ({len(values)} fields)")
    count = int(values[1])
    if values[1] != count or len(values) != 4 + count * 55:
        raise AssertionError(f"{label}: inconsistent patch count or field count")
    return values


def compare(label: str, expected: list[float], actual: list[float]) -> None:
    if len(expected) != len(actual):
        raise AssertionError(f"{label}: {len(actual)} fields != {len(expected)}")
    for index, (want, got) in enumerate(zip(expected, actual)):
        if not math.isclose(want, got, rel_tol=2e-10, abs_tol=2e-11):
            raise AssertionError(f"{label} field {index}: DynLex {got} != Rust {want}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--compiler", type=Path, default=ROOT / "build/dynlex.exe")
    parser.add_argument("--reference-root", type=Path, default=ROOT / "build/cadkernel-upstream-offset-reference")
    parser.add_argument("--rustc", type=Path, default=Path("rustc"))
    args = parser.parse_args()
    source = args.source.resolve()
    revision = run(["git", "-c", f"safe.directory={source.as_posix()}", "-C", source, "rev-parse", "HEAD"])
    if revision != PIN:
        raise AssertionError(f"source revision {revision} != {PIN}")
    digest = hashlib.sha256((source / "src/brep/sweep_path.rs").read_bytes()).hexdigest()
    if digest != SOURCE_HASH:
        raise AssertionError(f"source hash {digest} != {SOURCE_HASH}")
    suffix = ".exe" if sys.platform == "win32" else ".out"
    previous: dict[str, list[float]] = {}
    with tempfile.TemporaryDirectory(prefix="cad-path-fit-") as temporary:
        output = Path(temporary)
        for mode, directory in (("O0", "debug"), ("O2", "release")):
            library = args.reference_root / directory / "libcadkernel.rlib"
            dependencies = args.reference_root / directory / "deps"
            if not library.is_file() or not dependencies.is_dir():
                raise AssertionError(f"missing pinned Rust library: {library}")
            reference = output / f"reference-{mode}{suffix}"
            native = output / f"native-{mode}{suffix}"
            run([args.rustc, "--edition=2021", HERE / "reference.rs", "--extern",
                 f"cadkernel={library.resolve()}", "-L", f"dependency={dependencies.resolve()}",
                 "-C", f"opt-level={mode[1]}", "-o", reference])
            run([args.compiler, HERE / "probe.dl", f"-{mode}", "-o", native])
            for case, name in enumerate(CASES):
                expected = numbers(f"Rust/{name}/{mode}", run([reference, str(case)]))
                actual = numbers(f"DynLex/{name}/{mode}", run([native, str(case)]))
                compare(f"{name}/{mode}", expected, actual)
                if mode == "O2":
                    compare(f"{name}/O0-O2", previous[name], actual)
                else:
                    previous[name] = actual
                if case == 1 and actual[1] <= 1:
                    raise AssertionError(f"{mode}: quartic fit did not subdivide")
                if case == 2 and actual[:2] != [0.0, 0.0]:
                    raise AssertionError(f"{mode}: refused evaluator retained patches")
                print(f"{name}/{mode}: {len(actual)} fields", flush=True)
    print(f"PASS: {len(CASES)} pinned fit/regularity cases at O0/O2", flush=True)


if __name__ == "__main__":
    main()
