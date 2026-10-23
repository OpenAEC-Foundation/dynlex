#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare pinned Rust and DynLex deformed sweeps at O0 and O2."""
from __future__ import annotations

import argparse
from dataclasses import dataclass
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
FIELDS = (
    "vertices", "edges", "coedges", "loops", "faces", "shells", "lumps",
    "surfaces", "curves", "roots", "flaws", "two-coedge-edges", "pcurves",
    "forward-faces",
)
XY = (0., 0., 0., 1., 0., 0., 0., 1., 0.)
STRAIGHT = (0., 0., 0., 0., 1., 0., 0., 0., 1.)
ARC = (3., 0., 0., 0., 0., 1., 1., 0., 0.)
MIXED = (3., -2., 0., 0., 0., 1., 1., 0., 0.)


def line(start: tuple[float, float], end: tuple[float, float]) -> tuple[float, ...]:
    return (0., *start, *end)


def arc(radius: float = 3., start: float = 0., end: float = math.pi / 2) -> tuple[float, ...]:
    return (2., 0., 0., radius, start, end)


def circle(radius: float = 0.5) -> tuple[float, ...]:
    return (1., 0., 0., radius)


def ring(points: tuple[tuple[float, float], ...]) -> tuple[tuple[float, ...], ...]:
    return tuple(line(points[index], points[(index + 1) % len(points)]) for index in range(len(points)))


TRIANGLE = ring(((0., 0.), (1., 0.), (0.5, 1.)))
RECTANGLE = ring(((-0.5, -0.5), (0.5, -0.5), (0.5, 0.5), (-0.5, 0.5)))
ARC_TRIANGLE = ring(((0., 0.), (0., 1.), (1., 0.5)))
LINE_PATH = (line((0., 0.), (3., 0.)),)
BENT_PATH = (line((0., 0.), (3., 0.)), line((3., 0.), (3., 3.)))
CURVED_PATH = (arc(),)
MIXED_PATH = (line((3., -2.), (3., 0.)), arc())


@dataclass(frozen=True)
class Case:
    name: str
    rotation: float = 0.
    twist: float = 0.
    scale: float = 1.
    profile_plane: tuple[float, ...] = STRAIGHT
    path_plane: tuple[float, ...] = XY
    profile: tuple[tuple[float, ...], ...] = TRIANGLE
    path: tuple[tuple[float, ...], ...] = LINE_PATH
    valid: bool = True

    def arguments(self) -> list[str]:
        values = [*self.profile_plane, *self.path_plane, self.rotation, self.twist, self.scale]
        for curves in (self.profile, self.path):
            values.append(len(curves))
            for curve in curves:
                values.extend(curve)
        return [format(float(value), ".17g") for value in values]


def cases() -> list[Case]:
    return [
        Case("identity-line"),
        Case("identity-bend", path=BENT_PATH),
        Case("identity-arc", profile_plane=ARC, profile=ARC_TRIANGLE, path=CURVED_PATH),
        Case("identity-circle-profile-refused", profile=(circle(),), valid=False),
        Case("rotation-line", rotation=0.4),
        Case("negative-rotation", rotation=-0.7),
        Case("twist-line", twist=0.5),
        Case("negative-twist", twist=-0.5),
        Case("scale-line", scale=1.5),
        Case("shrink-line", scale=0.7),
        Case("combined-line", rotation=0.15, twist=0.4, scale=1.2, profile=RECTANGLE),
        Case("twist-subdivisions", twist=math.pi / 2),
        Case("right-bend", rotation=0.1, twist=0.2, scale=1.2, path=BENT_PATH),
        Case("reversed-piece", rotation=0.1, twist=0.2, scale=1.2,
             path=(line((0., 0.), (3., 0.)), line((3., 3.), (3., 0.)))),
        Case("oblique-bend", twist=0.2, scale=1.1,
             path=(line((0., 0.), (3., 0.)), line((3., 0.), (5., 2.)))),
        Case("arc-path", rotation=0.1, twist=0.2, scale=1.2,
             profile_plane=ARC, profile=TRIANGLE, path=CURVED_PATH),
        Case("arc-path-negative-twist", rotation=0.1, twist=-0.2, scale=1.,
             profile_plane=ARC, profile=TRIANGLE, path=CURVED_PATH),
        Case("mixed-line-arc", rotation=0.05, twist=0.2, scale=1.1,
             profile_plane=MIXED, profile=ring(((0., 0.), (0.5, 0.), (0.25, 0.5))), path=MIXED_PATH),
        Case("invalid-arc-profile-geometry", rotation=0.1, twist=0.2, scale=1.2,
             profile_plane=ARC, profile=ARC_TRIANGLE, path=CURVED_PATH, valid=False),
        Case("invalid-zero-scale", rotation=0.1, scale=0., valid=False),
        Case("invalid-negative-scale", rotation=0.1, scale=-1., valid=False),
        Case("invalid-minimum-scale", rotation=0.1, scale=1e-9, valid=False),
        Case("invalid-infinite-rotation", rotation=math.inf, valid=False),
        Case("invalid-nan-twist", twist=math.nan, valid=False),
        Case("invalid-infinite-scale", scale=math.inf, valid=False),
        Case("invalid-excessive-twist", twist=800., valid=False),
        Case("invalid-curved-profile", rotation=0.1, profile=(circle(),), valid=False),
        Case("invalid-open-profile", rotation=0.1, profile=TRIANGLE[:1], valid=False),
        Case("invalid-empty-profile", rotation=0.1, profile=(), valid=False),
        Case("invalid-empty-path", rotation=0.1, path=(), valid=False),
        Case("invalid-disconnected-path", rotation=0.1,
             path=(line((0., 0.), (2., 0.)), line((4., 0.), (5., 0.))), valid=False),
        Case("invalid-stationary-path", rotation=0.1,
             path=(line((0., 0.), (0., 0.)),), valid=False),
        Case("invalid-closed-path", rotation=0.1,
             path=(line((0., 0.), (3., 0.)), line((3., 0.), (0., 0.))), valid=False),
        Case("invalid-reversing-joint", rotation=0.1,
             path=(line((0., 0.), (3., 0.)), line((3., 0.), (1., 0.))), valid=False),
        Case("invalid-off-plane-profile", rotation=0.1,
             profile_plane=(1., 0., 0., *STRAIGHT[3:]), valid=False),
        Case("invalid-circle-path", rotation=0.1, path=(circle(3.),), valid=False),
    ]


def run(command: list[str | Path], *, timeout: float = 240) -> str:
    status, output, _ = fixtures.run_process(
        [str(part) for part in command], timeout=timeout, cwd=ROOT,
    )
    if status:
        raise AssertionError(f"exit {status}: {command}\n{output}")
    return output.strip()


def observe(output: str, expected_valid: bool) -> tuple[tuple[int, ...], list[tuple[float, float, float]]]:
    try:
        numbers = [float(token) for token in output.split()]
    except ValueError as error:
        raise AssertionError(f"nonnumeric probe output: {output!r}") from error
    if not numbers or numbers[0] != float(expected_valid):
        raise AssertionError(f"validity differs from case expectation: {output!r}")
    if not expected_valid:
        if len(numbers) != 1:
            raise AssertionError("invalid body emitted extra fields")
        return (), []
    if len(numbers) < 1 + len(FIELDS):
        raise AssertionError("valid body lacks arena statistics")
    values = numbers[1:1 + len(FIELDS)]
    if not all(math.isfinite(value) and value >= 0 and value == int(value) for value in values):
        raise AssertionError("invalid arena statistic")
    fields = tuple(map(int, values))
    if fields[9] != 1 or fields[10] != 0 or fields[11] != fields[1]:
        raise AssertionError(f"nonmanifold or invalid body: {fields}")
    coordinates = numbers[1 + len(FIELDS):]
    if len(coordinates) != 3 * fields[0] or not all(map(math.isfinite, coordinates)):
        raise AssertionError("invalid vertex coordinates")
    vertices = [tuple(coordinates[index:index + 3]) for index in range(0, len(coordinates), 3)]
    return fields, sorted(vertices, key=lambda point: (tuple(round(value, 8) for value in point), point))


def compare(name: str, expected: tuple, actual: tuple) -> int:
    left_fields, left_vertices = expected
    right_fields, right_vertices = actual
    for field, left, right in zip(FIELDS, left_fields, right_fields):
        if left != right:
            raise AssertionError(f"{name}: {field}: Rust={left}, DynLex={right}")
    if len(left_vertices) != len(right_vertices):
        raise AssertionError(f"{name}: vertex count differs")
    for index, (left, right) in enumerate(zip(left_vertices, right_vertices)):
        for axis, (source, native) in enumerate(zip(left, right)):
            if not math.isclose(source, native, rel_tol=1e-10, abs_tol=1e-8):
                raise AssertionError(f"{name}: vertex {index} axis {axis}: Rust={source}, DynLex={native}")
    return 1 + len(left_fields) + len(left_vertices) * 3 if left_fields else 1


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--reference-root", type=Path, default=ROOT / "build/cadkernel-upstream-offset-reference")
    parser.add_argument("--compiler", type=Path, default=ROOT / "build/dynlex.exe")
    parser.add_argument("--rustc", type=Path, default=Path("rustc"))
    parser.add_argument("--case", default="", help="run cases containing this text")
    parser.add_argument("--output", type=Path, help="retain compiled probes in this directory")
    parser.add_argument("--skip-build", action="store_true", help="reuse compiled probes")
    arguments = parser.parse_args()

    source = arguments.source.resolve()
    revision = run(["git", "-c", f"safe.directory={source.as_posix()}", "-C", source, "rev-parse", "HEAD"])
    if revision != PIN:
        raise AssertionError(f"source revision {revision} != {PIN}")
    source_hash = hashlib.sha256((source / "src/brep/sweep.rs").read_bytes()).hexdigest()
    if source_hash != SWEEP_HASH:
        raise AssertionError(f"source hash {source_hash} != {SWEEP_HASH}")
    selected = [case for case in cases() if arguments.case in case.name]
    if not selected:
        raise AssertionError("no cases selected")
    compiler_hash = hashlib.sha256(arguments.compiler.read_bytes()).hexdigest()

    suffix = ".exe" if sys.platform == "win32" else ".out"
    comparisons = 0
    temporary = None if arguments.output else tempfile.TemporaryDirectory(prefix="cad-deformed-diff-")
    try:
        output = arguments.output.resolve() if arguments.output else Path(temporary.name)
        output.mkdir(parents=True, exist_ok=True)
        binaries = {}
        for mode, rust_directory in (("O0", "debug"), ("O2", "release")):
            library = (arguments.reference_root / rust_directory / "libcadkernel.rlib").resolve()
            dependencies = (arguments.reference_root / rust_directory / "deps").resolve()
            if not library.is_file() or not dependencies.is_dir():
                raise AssertionError(f"missing pinned Rust library: {library}")
            rust_binary = output / f"reference-{mode}{suffix}"
            native_binary = output / f"native-{mode}{suffix}"
            if not arguments.skip_build:
                run([
                    arguments.rustc, "--edition=2021", HERE / "reference.rs",
                    "--extern", f"cadkernel={library}", "-L", f"dependency={dependencies}",
                    "-C", f"opt-level={mode[1]}", "-o", rust_binary,
                ])
                diagnostics = run([arguments.compiler.resolve(), HERE / "probe.dl", f"-{mode}", "-o", native_binary])
                if diagnostics:
                    raise AssertionError(f"{mode} DynLex diagnostics:\n{diagnostics}")
            if not rust_binary.is_file() or not native_binary.is_file():
                raise AssertionError(f"missing {mode} probes in {output}")
            binaries[mode] = {"Rust": rust_binary, "DynLex": native_binary}
            print(f"{mode}: probes ready", flush=True)

        failures = []
        for case in selected:
            results = {}
            for mode in ("O0", "O2"):
                for language in ("Rust", "DynLex"):
                    label = f"{case.name}/{mode}/{language}"
                    try:
                        results[(mode, language)] = observe(
                            run([binaries[mode][language], *case.arguments()], timeout=30), case.valid,
                        )
                    except AssertionError as error:
                        failures.append(f"{label}: {error}")
                if (mode, "Rust") in results and (mode, "DynLex") in results:
                    try:
                        comparisons += compare(
                            f"{case.name}/{mode}", results[(mode, "Rust")], results[(mode, "DynLex")],
                        )
                    except AssertionError as error:
                        failures.append(str(error))
            for language in ("Rust", "DynLex"):
                if ("O0", language) in results and ("O2", language) in results:
                    try:
                        compare(f"{case.name}/{language} O0-O2", results[("O0", language)], results[("O2", language)])
                    except AssertionError as error:
                        failures.append(str(error))
            if not any(failure.startswith(case.name + "/") or failure.startswith(case.name + ":") for failure in failures):
                print(f"PASS {case.name}", flush=True)
        for failure in failures:
            print(f"FAIL {failure}", flush=True)
        if failures:
            raise AssertionError(f"{len(failures)} differential comparisons failed")
    finally:
        if temporary:
            temporary.cleanup()
    if hashlib.sha256(arguments.compiler.read_bytes()).hexdigest() != compiler_hash:
        raise AssertionError("compiler changed during comparison")
    print(f"PASS: {len(selected)} cases, {comparisons} Rust-DynLex fields at O0/O2; pin={PIN}")


if __name__ == "__main__":
    main()
