#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare public rational revolutions with pinned Rust at O0 and O2."""
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


def run(command: list[str | Path], timeout: float = 240) -> str:
    status, output, _ = fixtures.run_process([str(part) for part in command], timeout=timeout, cwd=ROOT)
    if status:
        raise AssertionError(f"exit {status}: {command}\n{output}")
    return output.strip()


def canonical(output: str) -> list[float]:
    numbers = [float(token) for token in output.split()]
    if len(numbers) == 1:
        return numbers
    use_count = int(numbers[3])
    vertex_count = int(numbers[1])
    face_count = int(numbers[5])
    cursor = 14
    for _ in range(use_count):
        pcurve = numbers[cursor + 1]
        cursor += 2 + (4 if pcurve == 1 else 0)
    fixed = numbers[:cursor]
    vertices = [tuple(numbers[cursor + 3 * index:cursor + 3 * index + 3])
                for index in range(vertex_count)]
    cursor += 3 * vertex_count
    faces = [tuple(numbers[cursor + 2 * index:cursor + 2 * index + 2])
             for index in range(face_count)]
    cursor += 2 * face_count
    if cursor != len(numbers):
        raise AssertionError(f"unparsed probe fields: {len(numbers) - cursor}")
    vertices.sort(key=lambda point: tuple(round(value, 12) for value in point))
    faces.sort()
    return fixed + [value for point in vertices for value in point] + [value for face in faces for value in face]


def compare(name: str, reference: str, actual: str, valid: bool) -> None:
    expected = canonical(reference)
    observed = canonical(actual)
    if len(expected) != len(observed):
        raise AssertionError(f"{name}: output length {len(observed)} != {len(expected)}")
    if not expected or expected[0] != float(valid):
        raise AssertionError(f"{name}: unexpected reference validity {expected[:1]}")
    if valid and (expected[11] != 0 or expected[12] < 1 or expected[13] < 2):
        raise AssertionError(f"{name}: invalid reference topology {expected[:14]}")
    for index, (want, got) in enumerate(zip(expected, observed)):
        if not math.isfinite(got) or not math.isclose(want, got, rel_tol=2e-12, abs_tol=2e-12):
            raise AssertionError(f"{name} field {index}: DynLex {got} != Rust {want}")


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
    digest = hashlib.sha256((source / "src/brep/sweep.rs").read_bytes()).hexdigest()
    if digest != SWEEP_HASH:
        raise AssertionError(f"source hash {digest} != {SWEEP_HASH}")
    cases = []
    for operation in (0, 1):
        for kind in (3, 5):
            for label, angle, side, axis_end, valid in (
                ("partial", math.pi / 2, 1, 0, True),
                ("full", math.tau, 1, 0, True),
                ("negative", -math.pi / 2, 1, 0, True),
                ("reflex", 3 * math.pi / 2, 1, 0, True),
                ("far-side", math.pi / 2, -1, 0, True),
                ("axis", math.pi / 2, 0, 0, False),
                ("axis-ended-partial", math.pi / 2, 1 / 3 if kind == 3 else 1, kind == 5, True),
                ("axis-ended-full", math.tau, 1 / 3 if kind == 3 else 1, kind == 5, True),
            ):
                if operation == 1 and label == "axis-ended-partial":
                    valid = False
                cases.append((f"{operation}-{kind}-{label}", [operation, kind, angle, side, axis_end], valid))
    suffix = ".exe" if sys.platform == "win32" else ".out"
    with tempfile.TemporaryDirectory(prefix="cad-revolve-public-") as temporary:
        output = Path(temporary)
        for mode, directory in (("O0", "debug"), ("O2", "release")):
            library = args.reference_root / directory / "libcadkernel.rlib"
            dependencies = args.reference_root / directory / "deps"
            if not library.is_file() or not dependencies.is_dir():
                raise AssertionError(f"missing pinned Rust library: {library}")
            rust_binary = output / f"reference-{mode}{suffix}"
            native_binary = output / f"native-{mode}{suffix}"
            run([args.rustc, "--edition=2021", HERE / "reference.rs", "--extern",
                 f"cadkernel={library.resolve()}", "-L", f"dependency={dependencies.resolve()}",
                 "-C", f"opt-level={mode[1]}", "-o", rust_binary])
            run([args.compiler, HERE / "probe.dl", f"-{mode}", "-o", native_binary])
            for name, values, valid in cases:
                numbers = [format(float(value), ".17g") for value in values]
                reference = run([rust_binary, *numbers])
                actual = run([native_binary, *numbers])
                compare(f"{name}/{mode}", reference, actual, valid)
                print(f"{name}/{mode}: pass", flush=True)
    print(f"PASS: {len(cases)} public cases at O0/O2", flush=True)


if __name__ == "__main__":
    main()
