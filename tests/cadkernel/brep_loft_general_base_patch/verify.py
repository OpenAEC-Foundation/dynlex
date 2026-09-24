#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare pinned Rust homogeneous base_patch() with DynLex at O0/O2."""
from __future__ import annotations

import argparse
from dataclasses import dataclass, replace
import hashlib
import math
from pathlib import Path
import random
import shutil
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "tests/cadkernel"))
import verify as fixtures

PIN = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"
HASHES = {
    "src/brep/loft_general.rs": "e0e38b79eb944f6b4e5a2fa263c7deb6b12e4ff2d2da238d327720fbdaf4fd98",
    "src/brep/nurbs_builder.rs": "01c546b09eb916f40e8b0d6c2281499260fa7df1767f9339dbf431481ae91f34",
}


def run(command: list[str | Path], *, timeout: float = 180) -> str:
    code, output, _ = fixtures.run_process([str(item) for item in command], timeout=timeout, cwd=ROOT)
    if code:
        raise AssertionError(f"exit {code}: {command}\n{output}")
    return output.strip()


def source_function(text: str, signature: str, start_at: int = 0) -> str:
    start = text.index(signature, start_at)
    first_brace = text.index("{", start)
    depth = 0
    for index in range(first_brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start:index + 1]
    raise AssertionError(f"unclosed source function {signature}")


def driver_text(source: Path) -> str:
    text = (source / "src/brep/loft_general.rs").read_text(encoding="utf-8")
    definitions = text[text.index("/// A planar section may contain"):text.index("impl Bezier {")]
    bezier = text.index("impl Bezier {")
    wire = text.index("impl Wire {")
    methods = "impl Bezier {\n" + "\n".join(source_function(text, signature, bezier) for signature in (
        "fn degree(&self)", "fn point(&self, t: f64)", "fn reversed(&self)",
        "fn split(&self, t: f64)", "fn part(&self, a: f64, b: f64)",
        "fn elevated(&self, degree: usize)", "fn unit_end_weights(&self)",
    )) + "\n}\nimpl Wire {\n" + "\n".join(source_function(text, signature, wire) for signature in (
        "fn point(&self, t: f64)", "fn part(&self, a: f64, b: f64)",
        "fn reversed(&self)", "fn rotated(&self, at: f64)",
    )) + "\n}\n"
    functions = "\n\n".join(source_function(text, signature) for signature in (
        "fn mix(", "fn project(", "fn homogeneous(", "fn de_casteljau(",
        "fn unique(", "fn rational_spans(", "fn section_wire(", "fn distance(",
        "fn geometry_tolerance(", "fn rotate_between(", "fn wire_centre(", "fn prepared(",
        "fn derivative(", "fn section_derivative(", "fn base_patch(",
    ))
    source_module = source / "src/brep/nurbs_builder.rs"
    result = f'#[path = r#"{source_module.as_posix()}"#] mod nurbs_builder;\n'
    result += (HERE / "reference.rs").read_text(encoding="utf-8")
    return (result.replace("// PINNED_SOURCE_DEFINITIONS", definitions)
                  .replace("// PINNED_SOURCE_METHODS", methods)
                  .replace("// PINNED_SOURCE_FUNCTIONS", functions))


def line(a: tuple[float, float], b: tuple[float, float]) -> tuple[float, ...]:
    return (0., *a, *b)


def ring(points: tuple[tuple[float, float], ...], shift: int = 0,
         reverse: bool = False, mask: int = 0) -> tuple[tuple[float, ...], ...]:
    edges = [line(points[index], points[(index + 1) % len(points)]) for index in range(len(points))]
    edges = edges[shift:] + edges[:shift]
    if reverse:
        edges.reverse()
    for index, edge in enumerate(edges):
        if mask & (1 << index):
            edges[index] = line((edge[3], edge[4]), (edge[1], edge[2]))
    return tuple(edges)


def polygon_spline(points: tuple[tuple[float, float], ...],
                   knots: tuple[float, ...]) -> tuple[float, ...]:
    controls = (*points, points[0])
    coordinates = tuple(value for point in controls for value in point)
    return (4., 1., float(len(controls)), *coordinates, float(len(knots)),
            *knots, float(len(controls)), *((1.,) * len(controls)))


RECT = ((-2., -1.), (2., -1.), (2., 1.), (-2., 1.))
HOLE = ((-.5, -.5), (-.5, .5), (.5, .5), (.5, -.5))


def frame(z: float = 0., x: float = 0., y: float = 0., angle: float = 0.,
          flip: bool = False, shear: float = 0.) -> tuple[float, ...]:
    c = math.cos(angle)
    s = math.sin(angle)
    axis_x = (c, s, 0.)
    axis_y = (-s + shear, c, 0.)
    if flip:
        axis_y = tuple(-value for value in axis_y)
    return (x, y, z, *axis_x, *axis_y)


@dataclass(frozen=True)
class Section:
    plane: tuple[float, ...]
    wires: tuple[tuple[tuple[float, ...], ...], ...]
    closed: bool = True


@dataclass(frozen=True)
class Case:
    name: str
    sections: tuple[Section, ...]
    cyclic: bool = False
    matching: bool = True
    mode: int = 1
    periodic: bool = True
    start_angle: float = math.pi / 2
    end_angle: float = math.pi / 2
    start_magnitude: float = 0.
    end_magnitude: float = 0.
    wire_index: int = 0
    band: int = 0
    error: int = 0

    def arguments(self) -> list[str]:
        values = [int(self.cyclic), int(self.matching), self.mode, int(self.periodic),
                  self.start_angle, self.end_angle, self.start_magnitude, self.end_magnitude,
                  self.wire_index, self.band, len(self.sections)]
        for section in self.sections:
            values.extend(section.plane)
            values.extend((int(section.closed), len(section.wires)))
            for wire in section.wires:
                values.append(len(wire))
                for curve in wire:
                    values.extend(curve)
        return [format(float(value), ".17g") for value in values]


def profile(z: float, outer: tuple[tuple[float, ...], ...] | None = None,
            hole: tuple[tuple[float, ...], ...] | None = None,
            **frame_options: float | bool) -> Section:
    wires = (outer if outer is not None else ring(RECT),)
    if hole is not None:
        wires += (hole,)
    return Section(frame(z, **frame_options), wires)


def cases() -> list[Case]:
    base = ring(RECT)
    shifted = ring(RECT, shift=2)
    reversed_order = ring(RECT, reverse=True)
    hole = ring(HOLE)
    polygon_knots = (0., 0., .1, .4, .9, 1., 1.)
    spline = polygon_spline(RECT, polygon_knots)
    shifted_spline = polygon_spline(RECT[2:] + RECT[:2], polygon_knots)
    reversed_spline = polygon_spline(tuple(reversed(RECT)), polygon_knots)
    result = [
        Case("too-few-zero", (), error=1),
        Case("too-few-one", (profile(0.),), error=1),
        Case("closed-loft-needs-three", (profile(0.), profile(4.)), cyclic=True, error=2),
        Case("parallel-unchanged", (profile(0.), profile(4.))),
        Case("disabled-alignment-preserves-seam", (profile(0.), profile(4., outer=shifted)), matching=False),
        Case("closed-shifted-anchor", (profile(0.), profile(4., outer=shifted))),
        Case("closed-reversed-winding", (profile(0.), profile(4., outer=reversed_order))),
        Case("closed-shifted-and-reversed", (profile(0.), profile(4., outer=ring(RECT, shift=1, reverse=True, mask=5)))),
        Case("transport-flipped-plane-normal", (profile(0.), profile(4., flip=True))),
        Case("transport-sheared-plane", (profile(0.), profile(4., shear=.2))),
        Case("transport-twisted-plane", (profile(0.), profile(4., angle=.4))),
        Case("transport-translated-plane", (profile(0., x=1e5, y=-1e5), profile(4., x=1e5 + .25, y=-1e5 - .25, outer=shifted))),
        Case("three-sections-progress", (profile(0.), profile(3., outer=shifted), profile(7., outer=reversed_order))),
        Case("three-sections-winding-flip", (profile(0.), profile(3., flip=True, outer=reversed_order), profile(7.))),
        Case("stationary-centres-fallback", (profile(0.), profile(0., outer=shifted), profile(0., outer=reversed_order))),
        Case("cyclic-three-sections", (profile(0.), profile(4., outer=shifted), profile(8., outer=reversed_order)), cyclic=True),
        Case("cyclic-four-sections", (profile(0.), profile(2., outer=shifted), profile(4., outer=reversed_order), profile(6.)), cyclic=True),
        Case("holes-align-independently", (profile(0., hole=hole), profile(4., outer=shifted, hole=ring(HOLE, shift=2, reverse=True)))),
        Case("holes-with-flipped-plane", (profile(0., hole=hole), profile(4., hole=ring(HOLE, shift=1), flip=True))),
        Case("open-chains-reverse", (
            Section(frame(0.), ((line((0., 0.), (2., 0.)), line((2., 0.), (3., 1.))),), closed=False),
            Section(frame(4.), ((line((3., 1.), (2., 0.)), line((2., 0.), (0., 0.))),), closed=False),
        )),
        Case("open-chains-disabled", (
            Section(frame(0.), ((line((0., 0.), (2., 0.)),),), closed=False),
            Section(frame(4.), ((line((2., 0.), (0., 0.)),),), closed=False),
        ), matching=False),
        Case("circle-seam", (Section(frame(0.), (((1., 0., 0., 2.),),)),
                             Section(frame(4.), (((1., 0., 0., 2.),),)))),
        Case("ellipse-seam", (Section(frame(0.), (((3., 0., 0., 3., 1., 1., 0., 0., math.tau),),)),
                              Section(frame(4., angle=.3), (((3., 0., 0., 3., 1., 1., 0., 0., math.tau),),)))),
        Case("nonuniform-spline-seam", (Section(frame(0.), ((spline,),)),
                                        Section(frame(4.), ((shifted_spline,),)))),
        Case("nonuniform-spline-winding", (Section(frame(0.), ((spline,),)),
                                           Section(frame(4.), ((reversed_spline,),)))),
        Case("no-outer-wire", (Section(frame(0.), ()), profile(4.)), error=3),
        Case("invalid-plane", (profile(0.), Section((0., 0., 4., 1., 0., 0., 2., 0., 0.), (base,))), error=3),
        Case("open-profile-with-hole", (profile(0.), Section(frame(4.), (base, hole), closed=False)), error=3),
        Case("empty-wire", (profile(0.), Section(frame(4.), ((),))), error=4),
        Case("unsupported-ray", (profile(0.), Section(frame(4.), (((5., 0., 0., 1., 0.),),))), error=5),
        Case("disconnected-wire", (profile(0.), Section(frame(4.), ((line((0., 0.), (1., 0.)), line((4., 0.), (5., 0.))),))), error=6),
        Case("mismatched-wire-count", (profile(0., hole=hole), profile(4.)), error=7),
        Case("mismatched-open-closed", (profile(0.), Section(frame(4.), ((line((0., 0.), (1., 0.)),),), closed=False)), error=7),
    ]
    rng = random.Random(953546)
    for index in range(20):
        sides = rng.randint(3, 7)
        points = tuple((2. * math.cos(math.tau * step / sides),
                        2. * math.sin(math.tau * step / sides)) for step in range(sides))
        first = ring(points)
        second = ring(points, rng.randrange(sides), bool(rng.randrange(2)), rng.randrange(1 << sides))
        third = ring(points, rng.randrange(sides), bool(rng.randrange(2)), rng.randrange(1 << sides))
        result.append(Case(f"polygon-alignment-{index}", (
            profile(0., outer=first), profile(3., outer=second), profile(6., outer=third),
        ), cyclic=index % 5 == 0))
    result = [replace(item, error=(4 if item.name == "stationary-centres-fallback" else
                                   1 if item.error in (1, 2) else 2 if item.error else 0))
              for item in result]
    three = (profile(0.), profile(3., outer=shifted), profile(7., outer=reversed_order))
    for mode in range(7):
        result.append(Case(f"normal-mode-{mode}", three, mode=mode, band=1,
                           start_angle=.35, end_angle=1.1,
                           start_magnitude=2.5, end_magnitude=.75))
    for mode in (0, 1, 4, 5, 6):
        for periodic in (False, True):
            result.append(Case(f"cyclic-mode-{mode}-periodic-{int(periodic)}", three,
                               cyclic=True, mode=mode, periodic=periodic, band=2,
                               start_angle=.45, end_angle=1.2,
                               start_magnitude=1.5, end_magnitude=2.))
    result.extend([
        Case("hole-patch", (profile(0., hole=hole), profile(4., hole=ring(HOLE, shift=2))), wire_index=1),
        Case("coincident-refusal", (profile(0.), profile(0.)), error=4),
        Case("coincident-centres-profile-change", (
            profile(0.),
            profile(0., outer=ring(((-3., -2.), (3., -2.), (3., 2.), (-3., 2.)))),
        )),
        Case("invalid-normal-mode", (profile(0.), profile(4.)), mode=7, error=1),
        Case("invalid-negative-magnitude", (profile(0.), profile(4.)), start_magnitude=-1., error=1),
        Case("invalid-nonfinite-angle", (profile(0.), profile(4.)), start_angle=math.nan, error=1),
        Case("invalid-wire-index", (profile(0.), profile(4.)), wire_index=1, error=3),
        Case("invalid-band-index", (profile(0.), profile(4.)), band=1, error=3),
        Case("open-line-arc-degree-elevation", (
            Section(frame(0.), ((line((1., 0.), (-1., 0.)),),), closed=False),
            Section(frame(4.), (((2., 0., 0., 1., 0., math.pi),),), closed=False),
        )),
    ])
    return result


def compare(name: str, rust: str, native: str, expected_error: int) -> int:
    left = [float(line) for line in rust.splitlines()]
    right = [float(line) for line in native.splitlines()]
    if len(left) != len(right):
        raise AssertionError(f"{name}: field count Rust={len(left)} DynLex={len(right)}\n{rust}\n{native}")
    if left[:3] != right[:3] or int(left[1]) != expected_error:
        raise AssertionError(f"{name}: Rust status {left[:3]}, DynLex {right[:3]}, expected error {expected_error}")
    position = 3 + int(left[2])
    if left[position] != right[position]:
        raise AssertionError(f"{name}: patch count differs")
    patch_count = int(left[position]); position += 1
    for _ in range(patch_count):
        if left[position + 2] != right[position + 2]:
            raise AssertionError(f"{name}: row count differs")
        row_count = int(left[position + 2]); position += 3
        for _ in range(row_count):
            if left[position] != right[position]:
                raise AssertionError(f"{name}: control count differs")
            position += 1 + 4 * int(left[position])
    if position != len(left):
        raise AssertionError(f"{name}: malformed output")
    for index, (a, b) in enumerate(zip(left[3:], right[3:]), start=3):
        if not (math.isclose(a, b, rel_tol=5e-12, abs_tol=3e-10) or
                (math.isnan(a) and math.isnan(b))):
            raise AssertionError(f"{name}: field {index}: Rust={a}, DynLex={b}")
    return len(left)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--compiler", type=Path, default=ROOT / "build/dynlex.exe")
    parser.add_argument("--reference-root", type=Path,
                        help="prebuilt pinned O0/O2 Rust target; otherwise build from the pinned checkout")
    parser.add_argument("--cargo", type=Path, default=Path("cargo"))
    parser.add_argument("--rustc", type=Path, default=Path("rustc"))
    parser.add_argument("--case", default="")
    args = parser.parse_args()
    source = args.source.resolve()
    revision = run(["git", "-c", f"safe.directory={source.as_posix()}",
                    "-C", source, "rev-parse", "HEAD"])
    if revision != PIN:
        raise AssertionError(f"source revision {revision} != {PIN}")
    for path, expected in HASHES.items():
        actual = hashlib.sha256((source / path).read_bytes()).hexdigest()
        if actual != expected:
            raise AssertionError(f"source hash {path}: {actual} != {expected}")
    suffix = ".exe" if sys.platform == "win32" else ".out"
    tested = fields = 0
    with tempfile.TemporaryDirectory(prefix="cad-loft-general-base-", dir=ROOT / "build") as directory:
        temporary = Path(directory)
        if args.reference_root is None:
            source_copy = temporary / "source"
            shutil.copytree(source, source_copy)
            reference_root = temporary / "reference"
            for release in (False, True):
                command = [args.cargo, "build", "--offline", "--manifest-path", source_copy / "Cargo.toml",
                           "--target-dir", reference_root, "--package", "cadkernel", "--features", "brep"]
                if release:
                    command.append("--release")
                print(f"building pinned Rust reference {'O2' if release else 'O0'}", flush=True)
                run(command, timeout=600)
        else:
            reference_root = args.reference_root.resolve()
        driver = temporary / "driver.rs"
        driver.write_text(driver_text(source), encoding="utf-8")
        for mode, directory_name in (("O0", "debug"), ("O2", "release")):
            library = (reference_root / directory_name / "libcadkernel.rlib").resolve()
            dependencies = (reference_root / directory_name / "deps").resolve()
            if not library.is_file() or not dependencies.is_dir():
                raise AssertionError(f"missing pinned Rust library {library}")
            rust_binary = temporary / f"reference-{mode}{suffix}"
            native_binary = temporary / f"native-{mode}{suffix}"
            run([args.rustc, "--edition=2021", driver, "--extern", f"cadkernel={library}",
                 "-L", f"dependency={dependencies}", "-C", f"opt-level={mode[1]}",
                 "-A", "dead_code", "-o", rust_binary], timeout=240)
            run([args.compiler.resolve(), HERE / "probe.dl", f"-{mode}", "-o", native_binary], timeout=180)
            for item in cases():
                if args.case and args.case not in item.name:
                    continue
                parameters = item.arguments()
                rust = run([rust_binary, *parameters], timeout=15)
                native = run([native_binary, *parameters], timeout=15)
                fields += compare(f"{item.name}/{mode}", rust, native, item.error)
                tested += 1
            if not args.case:
                fixture = ROOT / "tests/required/cadkernel_brep_loft_general_base_patch"
                if fixture.is_dir():
                    print(f"fixture {mode}: {fixtures.verify_fixture(fixture, mode, args.compiler.resolve(), temporary, 180, 60, False)}", flush=True)
    if tested == 0:
        raise AssertionError("case filter selected no cases")
    print(f"PASS: {tested // 2} cases at O0/O2, {fields} compared fields; source={revision[:7]}", flush=True)


if __name__ == "__main__":
    main()
