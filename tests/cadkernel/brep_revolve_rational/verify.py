#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare isolated rational revolution geometry against pinned Rust output."""
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


def run(command: list[str | Path], timeout: float = 90) -> str:
    status, output, _ = fixtures.run_process([str(item) for item in command], timeout=timeout, cwd=ROOT)
    if status:
        raise AssertionError(f"exit {status}: {command}\n{output}")
    return output.strip()


def ellipse(name: str, angle: float, x: float, y: float, major: float, minor: float,
            start: float, end: float, axis: tuple[float, float] = (1, 0)) -> tuple[str, list[float]]:
    return name, [3, angle, x, y, major, minor, *axis, start, end]


def nurbs(name: str, angle: float, middle_x: float, middle_weight: float = 1,
          domain: tuple[float, float] = (0, 1)) -> tuple[str, list[float]]:
    a, b = domain
    return name, [5, angle, 1, 0, middle_x, 1, 1, 2, 1, middle_weight, 1, a, a, a, b, b, b]


CASES = [
    ellipse("ellipse-quarter", math.pi / 2, 3, 1, 1, .5, 0, math.pi),
    ellipse("ellipse-full-turn", math.tau, 3, 1, 1, .5, 0, math.pi),
    ellipse("ellipse-negative-turn", -math.pi / 2, 3, 1, 1, .5, 0, math.pi),
    ellipse("ellipse-far-side", math.pi / 2, -3, 1, 1, .5, 0, math.pi),
    ellipse("ellipse-rotated", math.pi / 2, 3, 1, 1, .5, -.3, 1.7,
            (math.sqrt(.5), math.sqrt(.5))),
    ellipse("ellipse-reflex", 3 * math.pi / 2, 3, 1, 1, .5, .2, 2.4),
    ellipse("ellipse-near-axis-crossing", math.pi / 2, 1, 1, 1.0000001, .5, 0, math.pi),
    ellipse("ellipse-crossing", math.pi / 2, 0, 1, 1, .5, 0, math.pi),
    nurbs("nurbs-hull-crossing", math.pi / 2, -.1),
    nurbs("nurbs-full-turn", math.tau, 2, 2),
    nurbs("nurbs-domain", math.pi / 2, 2, 2, (2, 6)),
    nurbs("nurbs-light-middle", math.pi / 2, -.1, .1),
    nurbs("nurbs-crossing", math.pi / 2, -2),
]


def compare(name: str, reference: str, actual: str) -> None:
    expected = [float(value) for value in reference.splitlines()]
    observed = [float(value) for value in actual.splitlines()]
    if len(expected) != len(observed):
        raise AssertionError(f"{name}: output field count {len(observed)} != {len(expected)}")
    for index, (want, got) in enumerate(zip(expected, observed)):
        if not math.isclose(want, got, rel_tol=2e-12, abs_tol=2e-12):
            raise AssertionError(f"{name} field {index}: DynLex {got} != Rust {want}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--compiler", type=Path, default=ROOT / "build/dynlex.exe")
    parser.add_argument("--reference-root", type=Path, default=ROOT / "build/cadkernel-upstream-offset-reference")
    parser.add_argument("--rustc", type=Path, default=Path("rustc"))
    arguments = parser.parse_args()
    source = arguments.source.resolve()
    revision = run(["git", "-c", f"safe.directory={source.as_posix()}", "-C", source, "rev-parse", "HEAD"])
    if revision != PIN:
        raise AssertionError(f"source revision {revision} != {PIN}")
    digest = hashlib.sha256((source / "src/brep/sweep.rs").read_bytes()).hexdigest()
    if digest != SWEEP_HASH:
        raise AssertionError(f"source hash {digest} != {SWEEP_HASH}")
    suffix = ".exe" if sys.platform == "win32" else ".out"
    with tempfile.TemporaryDirectory(prefix="cad-revolve-rational-") as temporary:
        output = Path(temporary)
        for mode, directory in (("O0", "debug"), ("O2", "release")):
            library = arguments.reference_root / directory / "libcadkernel.rlib"
            dependencies = arguments.reference_root / directory / "deps"
            if not library.is_file() or not dependencies.is_dir():
                raise AssertionError(f"missing pinned Rust library: {library}")
            rust_binary = output / f"reference-{mode}{suffix}"
            native_binary = output / f"native-{mode}{suffix}"
            run([arguments.rustc, "--edition=2021", HERE / "reference.rs", "--extern",
                 f"cadkernel={library.resolve()}", "-L", f"dependency={dependencies.resolve()}",
                 "-C", f"opt-level={mode[1]}", "-o", rust_binary])
            run([arguments.compiler, HERE / "probe.dl", f"-{mode}", "-o", native_binary])
            for name, data in CASES:
                numbers = [format(float(value), ".17g") for value in data]
                reference = run([rust_binary, *numbers])
                actual = run([native_binary, *numbers])
                compare(f"{name}/{mode}", reference, actual)
                print(f"{name}/{mode}: {len(reference.splitlines())} fields", flush=True)
    print(f"PASS: {len(CASES)} cases at O0/O2", flush=True)


if __name__ == "__main__":
    main()
