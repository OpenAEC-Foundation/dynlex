#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Differential for pinned general-loft section wires at O0 and O2."""
from __future__ import annotations

import argparse
from dataclasses import dataclass
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
XY = (0., 0., 0., 1., 0., 0., 0., 1., 0.)
TILTED = (3., -2., 5., 1., .1, .3, -.2, 2., .4)


def run(command: list[str | Path], *, timeout: float = 180) -> str:
    code, output, _ = fixtures.run_process(
        [str(item) for item in command], timeout=timeout, cwd=ROOT,
    )
    if code:
        raise AssertionError(f"exit {code}: {command}\n{output}")
    return output.strip()


def source_function(text: str, signature: str) -> str:
    start = text.index(signature)
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


def line(a: tuple[float, float], b: tuple[float, float]) -> tuple[float, ...]:
    return (0., *a, *b)


def circle(radius: float, centre: tuple[float, float] = (0., 0.)) -> tuple[float, ...]:
    return (1., *centre, radius)


def arc(radius: float, start: float, finish: float) -> tuple[float, ...]:
    return (2., 0., 0., radius, start, finish)


def ellipse(major: float, minor: float) -> tuple[float, ...]:
    return (3., 0., 0., major, minor, 1., 0., 0., math.tau)


def spline() -> tuple[float, ...]:
    points = ((0., 0.), (1., 2.), (2., 2.), (3., 0.))
    knots = (0., 0., 0., .4, 1., 1., 1.)
    weights = (1., .5, 2., 1.)
    return (4., 2., len(points), *(axis for point in points for axis in point),
            len(knots), *knots, len(weights), *weights)


def ring(points: tuple[tuple[float, float], ...]) -> tuple[tuple[float, ...], ...]:
    return tuple(line(points[index], points[(index + 1) % len(points)])
                 for index in range(len(points)))


OUTER = ring(((-3., -2.), (3., -2.), (3., 2.), (-3., 2.)))
HOLE = ring(((-1., -.5), (-1., .5), (1., .5), (1., -.5)))


@dataclass(frozen=True)
class Case:
    name: str
    closed: bool
    wires: tuple[tuple[tuple[float, ...], ...], ...]
    plane: tuple[float, ...] = XY
    error: int = 0

    def arguments(self) -> list[str]:
        values = [*self.plane, int(self.closed), len(self.wires)]
        for wire in self.wires:
            values.append(len(wire))
            for piece in wire:
                values.extend(piece)
        return [format(float(value), ".17g") for value in values]


def cases() -> list[Case]:
    a = line((0., 0.), (1., 0.))
    b = line((1., 0.), (1., 1.))
    c = line((1., 1.), (2., 1.))
    inner_far = ring(((10., 10.), (11., 10.), (11., 11.), (10., 11.)))
    result = [
        Case("open-single-line", False, ((a,),)),
        Case("open-ordered-three", False, ((a, b, c),)),
        Case("open-reverse-middle", False, ((a, line((1., 1.), (1., 0.)), c),)),
        Case("open-reverse-first", False, ((line((1., 0.), (0., 0.)), b),)),
        Case("open-reverse-all-order", False, ((c, b, a),)),
        Case("closed-square", True, (OUTER,)),
        Case("closed-reverse-piece-order", True, (tuple(reversed(OUTER)),)),
        Case("closed-reverse-some-edges", True, ((OUTER[0], line((3., 2.), (3., -2.)), OUTER[2], line((-3., -2.), (-3., 2.))),)),
        Case("closed-tilted-plane", True, (OUTER,), plane=TILTED),
        Case("closed-circle-rational-spans", True, ((circle(2.),),)),
        Case("closed-two-semicircles", True, ((arc(2., 0., math.pi), arc(2., math.pi, math.tau)),)),
        Case("closed-ellipse", True, ((ellipse(3., 1.),),)),
        Case("open-spline-and-line", False, ((spline(), line((3., 0.), (4., 0.))),)),
        Case("open-line-and-reversed-spline", False, ((line((4., 0.), (3., 0.)), spline()),)),
        Case("closed-one-hole", True, (OUTER, HOLE)),
        Case("closed-two-holes", True, (OUTER, HOLE, inner_far)),
        Case("closed-reverse-hole-order", True, (OUTER, tuple(reversed(HOLE)))),
        Case("separate-loop-not-contained-at-this-stage", True, (OUTER, inner_far)),
        Case("empty-profile", True, (), error=1),
        Case("invalid-plane", True, (OUTER,), plane=(0., 0., 0., 1., 0., 0., 2., 0., 0.), error=1),
        Case("open-profile-with-hole", False, (OUTER, HOLE), error=1),
        Case("empty-wire", False, ((),), error=2),
        Case("empty-hole", True, (OUTER, ()), error=2),
        Case("unsupported-ray", False, (((5., 0., 0., 1., 0.),),), error=3),
        Case("unsupported-xline", False, (((6., 0., 0., 1., 0.),),), error=3),
        Case("disconnected-open", False, ((a, line((4., 0.), (5., 0.))),), error=4),
        Case("disconnected-closed", True, ((a, b),), error=4),
        Case("disconnected-hole", True, (OUTER, (HOLE[0], line((9., 9.), (10., 9.)))), error=4),
        Case("near-join-within-tolerance", False, ((a, line((1. + 5e-10, 0.), (2., 0.))),)),
        Case("near-join-outside-tolerance", False, ((a, line((1. + 3e-9, 0.), (2., 0.))),), error=4),
        Case("large-origin-join-within-tolerance", False,
             ((line((1e9, 0.), (1e9 + 1., 0.)), line((1e9 + 1. + 1e-6, 0.), (1e9 + 2., 0.))),)),
        Case("large-origin-join-outside-tolerance", False,
             ((line((1e9, 0.), (1e9 + 1., 0.)), line((1e9 + 1. + 4e-6, 0.), (1e9 + 2., 0.))),), error=4),
    ]
    rng = random.Random(953546)
    for index in range(24):
        count = rng.randint(3, 7)
        points = tuple((2. * math.cos(math.tau * step / count),
                        2. * math.sin(math.tau * step / count)) for step in range(count))
        edges = list(ring(points))
        shift = rng.randrange(count)
        edges = edges[shift:] + edges[:shift]
        if index & 1:
            edges.reverse()
        for position in range(count):
            if rng.randrange(2):
                edge = edges[position]
                edges[position] = line((edge[3], edge[4]), (edge[1], edge[2]))
        result.append(Case(f"polygon-order-{index}", True, (tuple(edges),)))
    return result


def compare(name: str, rust: str, native: str, expected_error: int) -> int:
    left = [float(line) for line in rust.splitlines()]
    right = [float(line) for line in native.splitlines()]
    if len(left) != len(right):
        raise AssertionError(f"{name}: field count Rust={len(left)} DynLex={len(right)}\n{rust}\n{native}")
    if left[:3] != right[:3] or int(left[1]) != expected_error:
        raise AssertionError(f"{name}: Rust status {left[:3]}, DynLex {right[:3]}, expected error {expected_error}")
    position = 3
    for _ in range(int(left[2])):
        if left[position:position + 2] != right[position:position + 2]:
            raise AssertionError(f"{name}: wire flags or span count differ")
        span_count = int(left[position + 1])
        position += 2
        for _ in range(span_count):
            if left[position + 2] != right[position + 2]:
                raise AssertionError(f"{name}: control count differs")
            position += 3 + 4 * int(left[position + 2])
    if position != len(left):
        raise AssertionError(f"{name}: malformed output")
    for index, (a, b) in enumerate(zip(left[3:], right[3:]), start=3):
        if not (math.isclose(a, b, rel_tol=5e-13, abs_tol=3e-10) or
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
    rust_source = (source / "src/brep/loft_general.rs").read_text(encoding="utf-8")
    functions = "\n\n".join(source_function(rust_source, signature) for signature in (
        "fn mix(", "fn homogeneous(", "fn unique(", "fn rational_spans(",
        "fn section_wire(", "fn distance(", "fn geometry_tolerance(",
    ))
    suffix = ".exe" if sys.platform == "win32" else ".out"
    tested = fields = 0
    with tempfile.TemporaryDirectory(prefix="cad-loft-general-wire-", dir=ROOT / "build") as directory:
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
        source_module = source / "src/brep/nurbs_builder.rs"
        contents = f'#[path = r#"{source_module.as_posix()}"#] mod nurbs_builder;\n'
        contents += (HERE / "reference.rs").read_text(encoding="utf-8").replace(
            "// PINNED_SOURCE_FUNCTIONS", functions)
        driver.write_text(contents, encoding="utf-8")
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
                fixture = ROOT / "tests/required/cadkernel_brep_loft_general_wire"
                if fixture.is_dir():
                    print(f"fixture {mode}: {fixtures.verify_fixture(fixture, mode, args.compiler.resolve(), temporary, 180, 60, False)}", flush=True)
    if tested == 0:
        raise AssertionError("case filter selected no cases")
    print(f"PASS: {tested // 2} cases at O0/O2, {fields} compared fields; source={revision[:7]}", flush=True)


if __name__ == "__main__":
    main()
