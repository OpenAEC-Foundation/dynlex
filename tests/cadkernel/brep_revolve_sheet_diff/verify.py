#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare pinned Rust and DynLex sheet revolutions at O0 and O2."""
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
ARENAS = (
    "vertices", "edges", "coedges", "loops", "faces", "shells",
    "lumps", "surfaces", "curves",
)
KINDS = ("plane", "cylinder", "cone", "sphere", "torus", "nurbs")


def run(command: list[str | Path], *, timeout: float = 240) -> str:
    status, output, _ = fixtures.run_process(
        [str(part) for part in command], timeout=timeout, cwd=ROOT,
    )
    if status:
        raise AssertionError(f"exit {status}: {command}\n{output}")
    return output.strip()


def line(start: tuple[float, float], end: tuple[float, float]) -> list[float]:
    return [0, *start, *end]


def arc(
    centre: tuple[float, float], radius: float, start: float, end: float,
) -> list[float]:
    return [2, *centre, radius, start, end]


def circle(centre: tuple[float, float], radius: float) -> list[float]:
    return [1, *centre, radius]


def ring(points: list[tuple[float, float]]) -> list[list[float]]:
    return [line(points[index], points[(index + 1) % len(points)]) for index in range(len(points))]


@dataclass
class Case:
    name: str
    profiles: list[list[list[float]]]
    valid: bool
    mode: int = 0
    angle: float = math.pi / 2
    pivot: tuple[float, float, float] = (0.0, 0.0, 0.0)
    axis: tuple[float, float, float] = (0.0, 0.0, 1.0)

    def arguments(self) -> list[str]:
        values: list[float] = [
            self.mode, *self.pivot, *self.axis, self.angle, len(self.profiles),
        ]
        for profile in self.profiles:
            values.append(len(profile))
            for piece in profile:
                values.extend(piece)
        return [format(float(value), ".17g") for value in values]


def cases() -> list[Case]:
    vertical = [line((2, 0), (2, 4))]
    cone = [line((2, 0), (4, 5))]
    annulus = [line((2, 1), (4, 1))]
    torus = [arc((10, 0), 2, -math.pi / 2, math.pi / 2)]
    sphere = [arc((0, 0), 4, -math.pi / 2, math.pi / 2)]
    rectangle = ring([(2, 0), (5, 0), (5, 4), (2, 4)])
    inner = ring([(3, 1), (4, 1), (4, 3), (3, 3)])
    outer_axis = ring([(0, 0), (3, 0), (3, 4), (0, 4)])
    split_axis = [
        line((2, 0), (0, 0)), line((0, 0), (0, 3)), line((0, 3), (2, 3)),
    ]
    crossing = [line((-1, 0), (2, 0))]
    disconnected = [line((2, 0), (2, 1)), line((3, 0), (3, 1))]
    return [
        Case("open-vertical-quarter", [vertical], True),
        Case("open-vertical-full", [vertical], True, angle=math.tau),
        Case("open-vertical-negative-quarter", [vertical], True, angle=-math.pi / 2),
        Case("open-vertical-negative-full", [vertical], True, angle=-math.tau),
        Case("open-vertical-reflex", [vertical], True, angle=3 * math.pi / 2),
        Case("open-cone-reflex", [cone], True, angle=3 * math.pi / 2),
        Case("open-annulus-quarter", [annulus], True),
        Case("open-annulus-near-full", [annulus], True, angle=math.tau - 6e-10),
        Case("open-torus-quarter", [torus], True),
        Case("open-torus-full", [torus], True, angle=math.tau),
        Case("open-sphere-quarter", [sphere], True),
        Case("open-sphere-full", [sphere], True, angle=math.tau),
        Case("closed-rectangle-quarter", [rectangle], True),
        Case("closed-rectangle-full", [rectangle], True, angle=math.tau),
        Case("closed-axis-touch-quarter", [outer_axis], True),
        Case("closed-axis-touch-full", [outer_axis], True, angle=math.tau),
        Case("split-axis-components-full", [split_axis], True, angle=math.tau),
        Case("far-side-quarter", [[line((-2, 0), (-2, 4))]], True),
        Case("region-two-loops-quarter", [rectangle, inner], True, mode=1),
        Case("region-two-loops-full", [rectangle, inner], True, mode=1, angle=math.tau),
        Case("region-three-loops-full", [rectangle, inner, ring([(6, 0), (7, 0), (7, 2), (6, 2)])], True, mode=1, angle=math.tau),
        Case("region-mixed-chains-quarter", [rectangle, vertical], True, mode=1),
        Case("invalid-empty-profile", [[]], False),
        Case("invalid-empty-region", [], False, mode=1),
        Case("invalid-empty-region-member", [rectangle, []], False, mode=1),
        Case("invalid-zero-angle", [vertical], False, angle=0.0),
        Case("invalid-overturn", [vertical], False, angle=math.tau + 0.01),
        Case("invalid-cross-axis", [crossing], False),
        Case("invalid-collapsed-axis", [[line((0, 0), (0, 2))]], False),
        Case("invalid-disconnected-chain", [disconnected], False),
        Case("invalid-off-plane-axis", [vertical], False, axis=(0.0, 1.0, 1.0)),
        Case("invalid-zero-axis", [vertical], False, axis=(0.0, 0.0, 0.0)),
        Case("invalid-off-plane-pivot", [vertical], False, pivot=(0.0, 5.0, 0.0)),
        Case("invalid-region-member", [rectangle, crossing], False, mode=1),
        Case("invalid-circle-piece", [[circle((3, 1), 1)]], False),
    ]


def whole(value: float, description: str) -> int:
    if not value.is_integer() or value < 0:
        raise AssertionError(f"{description}: expected nonnegative integer, got {value}")
    return int(value)


@dataclass
class Observation:
    valid: bool
    counts: tuple[int, ...] = ()
    roots: int = 0
    flaws: int = 0
    kinds: tuple[int, ...] = ()
    boundary_edges: int = 0
    double_edges: int = 0
    pcurves: int = 0
    groups: tuple[tuple[int, ...], ...] = ()
    vertices: tuple[tuple[float, float, float], ...] = ()


def observation(output: str) -> Observation:
    try:
        numbers = [float(token) for token in output.split()]
    except ValueError as error:
        raise AssertionError(f"non-numeric probe output: {output!r}") from error
    if not numbers or not all(map(math.isfinite, numbers)) or numbers[0] not in (0.0, 1.0):
        raise AssertionError(f"invalid probe output: {output!r}")
    if numbers[0] == 0.0:
        if len(numbers) != 1:
            raise AssertionError("invalid body emitted extra fields")
        return Observation(False)
    if len(numbers) < 21:
        raise AssertionError(f"valid body emitted only {len(numbers)} fields")
    counts = tuple(whole(value, name) for value, name in zip(numbers[1:10], ARENAS))
    roots = whole(numbers[10], "roots")
    flaws = whole(numbers[11], "flaws")
    kinds = tuple(whole(value, name) for value, name in zip(numbers[12:18], KINDS))
    boundary, doubles, pcurves = (
        whole(value, name) for value, name in zip(
            numbers[18:21], ("boundary edges", "double edges", "pcurves"),
        )
    )
    index = 21
    groups = []
    for _ in range(roots):
        if index >= len(numbers):
            raise AssertionError("missing root shell count")
        shell_count = whole(numbers[index], "root shell count")
        index += 1
        if index + shell_count > len(numbers):
            raise AssertionError("missing shell face counts")
        groups.append(tuple(whole(value, "shell face count") for value in numbers[index:index + shell_count]))
        index += shell_count
    vertex_count = counts[0]
    coordinates = numbers[index:]
    if len(coordinates) != 3 * vertex_count:
        raise AssertionError(
            f"expected {vertex_count} vertex triples, got {len(coordinates)} coordinates"
        )
    vertices = tuple(
        tuple(coordinates[position:position + 3])
        for position in range(0, len(coordinates), 3)
    )
    if sum(kinds) != counts[7] or len(groups) != roots:
        raise AssertionError("surface or root grouping count is inconsistent")
    if sum(map(len, groups)) != counts[5] or sum(map(sum, groups)) != counts[4]:
        raise AssertionError("shell or face grouping count is inconsistent")
    if boundary + doubles != counts[1]:
        raise AssertionError("an edge has neither one nor two coedges")
    return Observation(
        True, counts, roots, flaws, kinds, boundary, doubles, pcurves,
        tuple(groups), vertices,
    )


def normalized(points: tuple[tuple[float, float, float], ...]) -> list[tuple[float, float, float]]:
    return sorted(points, key=lambda point: (tuple(round(value, 8) for value in point), point))


def compare(name: str, expected: Observation, actual: Observation) -> int:
    for field in (
        "valid", "counts", "roots", "flaws", "kinds",
        "boundary_edges", "double_edges", "pcurves",
    ):
        if getattr(expected, field) != getattr(actual, field):
            raise AssertionError(
                f"{name}: {field} DynLex={getattr(actual, field)} Rust={getattr(expected, field)}"
            )
    if sorted(expected.groups) != sorted(actual.groups):
        raise AssertionError(f"{name}: root shell/face groups DynLex={actual.groups} Rust={expected.groups}")
    source_points = normalized(expected.vertices)
    native_points = normalized(actual.vertices)
    if len(source_points) != len(native_points):
        raise AssertionError(f"{name}: vertex multiset sizes differ")
    for index, (source, native) in enumerate(zip(source_points, native_points)):
        for coordinate, (left, right) in enumerate(zip(source, native)):
            if not math.isclose(left, right, rel_tol=1e-10, abs_tol=1e-8):
                raise AssertionError(
                    f"{name}: sorted vertex {index} coordinate {coordinate} DynLex={right} Rust={left}"
                )
    if not expected.valid:
        return 1
    grouping_fields = sum(1 + len(group) for group in expected.groups)
    return 21 + grouping_fields + len(source_points) * 3


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--reference-root", type=Path, default=ROOT / "build/cadkernel-upstream-offset-reference")
    parser.add_argument("--compiler", type=Path, default=ROOT / "build/dynlex.exe")
    parser.add_argument("--rustc", type=Path, default=Path("rustc"))
    parser.add_argument("--case", default="", help="run case names containing this text")
    arguments = parser.parse_args()

    source = arguments.source.resolve()
    revision = run([
        "git", "-c", f"safe.directory={source.as_posix()}", "-C", source, "rev-parse", "HEAD",
    ])
    if revision != PIN:
        raise AssertionError(f"source revision {revision} != {PIN}")
    source_hash = hashlib.sha256((source / "src/brep/sweep.rs").read_bytes()).hexdigest()
    if source_hash != SWEEP_HASH:
        raise AssertionError(f"pinned sweep source hash changed: {source_hash}")
    compiler = arguments.compiler.resolve()
    compiler_hash = hashlib.sha256(compiler.read_bytes()).hexdigest()
    selected = [case for case in cases() if arguments.case in case.name]
    if not selected:
        raise AssertionError("no cases selected")

    binaries: dict[str, dict[str, Path]] = {}
    suffix = ".exe" if sys.platform == "win32" else ".out"
    with tempfile.TemporaryDirectory(prefix="cad-revolve-sheet-diff-") as temporary:
        output = Path(temporary)
        for mode, directory in (("O0", "debug"), ("O2", "release")):
            library = arguments.reference_root / directory / "libcadkernel.rlib"
            dependencies = arguments.reference_root / directory / "deps"
            if not library.is_file() or not dependencies.is_dir():
                raise AssertionError(f"missing pinned reference library: {library}")
            rust_binary = output / f"reference-{mode}{suffix}"
            native_binary = output / f"native-{mode}{suffix}"
            rust_diagnostics = run([
                arguments.rustc, "--edition=2021", HERE / "reference.rs",
                "--extern", f"cadkernel={library.resolve()}",
                "-L", f"dependency={dependencies.resolve()}",
                "-C", f"opt-level={mode[1]}", "-o", rust_binary,
            ])
            native_diagnostics = run([
                compiler, HERE / "probe.dl", f"-{mode}", "-o", native_binary,
            ])
            if rust_diagnostics:
                print(f"{mode} Rust diagnostics: {rust_diagnostics}", file=sys.stderr)
            if native_diagnostics:
                raise AssertionError(f"{mode} DynLex diagnostics:\n{native_diagnostics}")
            binaries[mode] = {"Rust": rust_binary, "DynLex": native_binary}
            print(f"{mode}: binaries ready", flush=True)

        failures: list[str] = []
        comparisons = 0
        for case in selected:
            observations: dict[tuple[str, str], Observation] = {}
            for mode in ("O0", "O2"):
                for language in ("Rust", "DynLex"):
                    label = f"{case.name}/{mode}/{language}"
                    try:
                        result = observation(run([binaries[mode][language], *case.arguments()], timeout=30))
                        if result.valid != case.valid:
                            raise AssertionError(f"validity={result.valid}, expected {case.valid}")
                        observations[(mode, language)] = result
                    except AssertionError as error:
                        failures.append(f"{label}: {error}")
                if (mode, "Rust") in observations and (mode, "DynLex") in observations:
                    try:
                        comparisons += compare(
                            f"{case.name}/{mode}",
                            observations[(mode, "Rust")],
                            observations[(mode, "DynLex")],
                        )
                    except AssertionError as error:
                        failures.append(str(error))
            for language in ("Rust", "DynLex"):
                if ("O0", language) in observations and ("O2", language) in observations:
                    try:
                        compare(
                            f"{case.name}/{language} O0-O2",
                            observations[("O0", language)],
                            observations[("O2", language)],
                        )
                    except AssertionError as error:
                        failures.append(str(error))
            if not any(failure.startswith(case.name + "/") for failure in failures):
                print(f"PASS {case.name}", flush=True)
        if hashlib.sha256(compiler.read_bytes()).hexdigest() != compiler_hash:
            raise AssertionError("compiler changed during comparison")
        for failure in failures:
            print(f"FAIL {failure}", flush=True)
        if failures:
            raise AssertionError(f"{len(failures)} differential comparisons failed")
        print(
            f"PASS: {len(selected)} cases, {comparisons} Rust-DynLex field comparisons "
            f"at O0/O2; pin={PIN}; compiler_sha256={compiler_hash}",
            flush=True,
        )


if __name__ == "__main__":
    main()
