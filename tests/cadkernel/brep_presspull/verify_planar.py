#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare planar-region booleans with the pinned Rust implementation."""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import random
import re
import runpy
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tests/cadkernel"))
import verify as fixtures

PIN = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"
HERE = ROOT / "tests/cadkernel/brep_presspull"
CROSS = runpy.run_path(str(ROOT / "tests/cadkernel/cross2/verify.py"))
CURVE, MEASURE = (CROSS[key] for key in ("CURVE", "MEASURE"))


class BodyReader:
    """Read the complete body-arena stream without depending on arena keys."""

    def __init__(self, text):
        self.values = []
        for line in text.splitlines():
            fields = line.split()
            self.values.append(float(fields[-1]))
        self.index = 0

    def number(self):
        value = self.values[self.index]
        self.index += 1
        return value

    def exact(self):
        value = self.number()
        rounded = int(value)
        if value != rounded:
            raise AssertionError(f"expected exact value at field {self.index - 1}, got {value}")
        return rounded

    def point(self, dimensions=3):
        return tuple(self.number() for _ in range(dimensions))

    def provenance(self):
        return self.exact(), self.exact()

    def keys(self):
        return tuple(self.exact() for _ in range(self.exact()))

    def scalars(self):
        return tuple(self.number() for _ in range(self.exact()))

    def plane(self):
        return self.point(), self.point(), self.point()

    def spline(self, dimensions):
        degree = self.exact()
        closed = self.exact()
        knots = self.scalars()
        weights = self.scalars()
        controls = tuple(self.point(dimensions) for _ in range(self.exact()))
        return degree, closed, knots, weights, controls

    def surface_spline(self):
        degrees = self.exact(), self.exact()
        periodicity = self.exact(), self.exact(), self.exact()
        knots = self.scalars(), self.scalars()
        weights = tuple(self.scalars() for _ in range(self.exact()))
        controls = tuple(
            tuple(self.point() for _ in range(self.exact()))
            for _ in range(self.exact())
        )
        return degrees, periodicity, knots, weights, controls


def body_summary(text):
    """Return key-independent topology and geometry from an emitted body."""
    reader = BodyReader(text)
    valid = reader.exact()
    kind = reader.exact()
    if not valid:
        if reader.index != len(reader.values):
            raise AssertionError("failure output has trailing fields")
        return {"valid": valid, "kind": kind}

    provenance = reader.provenance()
    roots = reader.keys()

    vertices = {}
    vertex_provenance = []
    for _ in range(reader.exact()):
        key = reader.exact()
        vertices[key] = reader.point()
        vertex_provenance.append(reader.provenance())

    edges = []
    for _ in range(reader.exact()):
        key = reader.exact()
        curve = reader.exact()
        parameters = reader.number(), reader.number()
        endpoints = reader.exact(), reader.exact()
        coedges = reader.keys()
        source = reader.provenance()
        edges.append((key, curve, parameters, endpoints, coedges, source))

    coedges = []
    pcurves = []
    for _ in range(reader.exact()):
        key = reader.exact()
        edge = reader.exact()
        forward = reader.exact()
        has_pcurve = reader.exact()
        if has_pcurve:
            curve_kind = reader.exact()
            if curve_kind != 0:
                raise AssertionError(f"unexpected emitted pcurve kind {curve_kind}")
            pcurves.append((reader.point(2), reader.point(2)))
        owner = reader.exact()
        source = reader.provenance()
        coedges.append((key, edge, forward, has_pcurve, owner, source))

    loops = []
    for _ in range(reader.exact()):
        key = reader.exact()
        uses = reader.keys()
        owner = reader.exact()
        source = reader.provenance()
        loops.append((key, uses, owner, source))

    faces = []
    for _ in range(reader.exact()):
        key = reader.exact()
        surface = reader.exact()
        forward = reader.exact()
        rings = reader.keys()
        owner = reader.exact()
        source = reader.provenance()
        faces.append((key, surface, forward, rings, owner, source))

    shells = []
    for _ in range(reader.exact()):
        key = reader.exact()
        shell_faces = reader.keys()
        owner = reader.exact()
        source = reader.provenance()
        shells.append((key, shell_faces, owner, source))

    lumps = []
    for _ in range(reader.exact()):
        key = reader.exact()
        lump_shells = reader.keys()
        source = reader.provenance()
        lumps.append((key, lump_shells, source))

    curves = {}
    curve_shapes = []
    for _ in range(reader.exact()):
        key = reader.exact()
        curve_kind = reader.exact()
        curves[key] = curve_kind
        if curve_kind == 0:
            reader.point()
            reader.point()
            curve_shapes.append((curve_kind,))
        elif curve_kind == 1:
            reader.plane()
            curve_shapes.append((curve_kind, reader.number()))
        elif curve_kind == 2:
            reader.plane()
            curve_shapes.append((curve_kind, reader.number(), reader.number()))
        elif curve_kind == 3:
            reader.plane()
            spline = reader.spline(2)
            curve_shapes.append((curve_kind, spline[0], spline[1], len(spline[2]), len(spline[4])))
        elif curve_kind == 4:
            spline = reader.spline(3)
            curve_shapes.append((curve_kind, spline[0], spline[1], len(spline[2]), len(spline[4])))
        else:
            raise AssertionError(f"unexpected curve kind {curve_kind}")

    surfaces = {}
    surface_shapes = []
    for _ in range(reader.exact()):
        key = reader.exact()
        surface_kind = reader.exact()
        surfaces[key] = surface_kind
        if surface_kind == 0:
            reader.plane()
            surface_shapes.append((surface_kind,))
        elif surface_kind == 1:
            reader.plane()
            surface_shapes.append((surface_kind, reader.number()))
        elif surface_kind == 2:
            reader.plane()
            surface_shapes.append((surface_kind, reader.number(), reader.number()))
        elif surface_kind in (3, 4):
            reader.plane()
            radii = (reader.number(),) if surface_kind == 3 else (reader.number(), reader.number())
            surface_shapes.append((surface_kind, *radii))
        elif surface_kind == 5:
            spline = reader.surface_spline()
            surface_shapes.append((surface_kind, *spline[0], *spline[1], len(spline[3]), len(spline[4])))
        else:
            raise AssertionError(f"unexpected surface kind {surface_kind}")

    euler = reader.exact()
    flaws = reader.exact()
    if reader.index != len(reader.values):
        raise AssertionError(f"body output has {len(reader.values) - reader.index} trailing fields")

    edge_segments = []
    for _, curve, parameters, endpoints, uses, source in edges:
        points = tuple(sorted((vertices[endpoints[0]], vertices[endpoints[1]])))
        edge_segments.append((curves[curve], points, len(uses), source[0], abs(parameters[1] - parameters[0])))

    loop_by_key = {key: uses for key, uses, _, _ in loops}
    return {
        "valid": valid,
        "kind": kind,
        "provenance": provenance[0],
        "root_count": len(roots),
        "counts": (len(vertices), len(edges), len(coedges), len(loops), len(faces),
                   len(shells), len(lumps), len(curves), len(surfaces)),
        "euler": euler,
        "flaws": flaws,
        "vertex_provenance": Counter(item[0] for item in vertex_provenance),
        "edge_degrees": sorted(len(item[4]) for item in edges),
        "coedge_pcurves": Counter(item[3] for item in coedges),
        "loop_degrees": sorted(len(item[1]) for item in loops),
        "face_degrees": sorted((len(item[3]), tuple(sorted(len(loop_by_key[key]) for key in item[3]))) for item in faces),
        "shell_degrees": sorted(len(item[1]) for item in shells),
        "lump_degrees": sorted(len(item[1]) for item in lumps),
        "curve_kinds": Counter(curves.values()),
        "surface_kinds": Counter(surfaces.values()),
        "curve_shapes": sorted(curve_shapes),
        "surface_shapes": sorted(surface_shapes),
        "points": sorted(vertices.values()),
        "edge_segments": sorted(edge_segments),
        "pcurves": sorted(tuple(sorted(segment)) for segment in pcurves),
    }


def same_number(left, right):
    if math.isnan(left) or math.isnan(right):
        return math.isnan(left) and math.isnan(right)
    if left == right:
        return True
    return abs(left - right) <= 5e-9 + math.ulp(max(1.0, abs(left), abs(right))) * 96.0


def compare_nested(left, right, path="result"):
    if isinstance(left, float) or isinstance(right, float):
        if not same_number(float(left), float(right)):
            raise AssertionError(f"{path}: native={left!r}, Rust={right!r}")
        return 1
    if type(left) is not type(right):
        raise AssertionError(f"{path}: native type={type(left).__name__}, Rust type={type(right).__name__}")
    if isinstance(left, dict):
        if left.keys() != right.keys():
            raise AssertionError(f"{path}: native keys={left.keys()}, Rust keys={right.keys()}")
        return sum(compare_nested(left[key], right[key], f"{path}.{key}") for key in left)
    if isinstance(left, (tuple, list)):
        if len(left) != len(right):
            raise AssertionError(f"{path}: native length={len(left)}, Rust length={len(right)}")
        return sum(compare_nested(a, b, f"{path}[{index}]") for index, (a, b) in enumerate(zip(left, right)))
    if left != right:
        raise AssertionError(f"{path}: native={left!r}, Rust={right!r}")
    return 1


def process(command, *, timeout=600):
    status, output, elapsed = fixtures.run_process(
        [str(part) for part in command], timeout=timeout, cwd=ROOT,
    )
    if status:
        raise AssertionError(f"exit {status}: {command}\n{output}")
    return output.strip(), elapsed


def encoded(operation, left, right, tolerance=1e-9, *,
            left_kind=0, right_kind=0, left_z=0.0, right_z=0.0):
    return [operation, *left, *right, tolerance,
            left_kind, right_kind, left_z, right_z]


def cases():
    layouts = (
        ("overlap", (0.0, 0.0, 2.0, 2.0), (1.0, 0.0, 3.0, 2.0)),
        ("contained", (0.0, 0.0, 4.0, 4.0), (1.0, 1.0, 3.0, 3.0)),
        ("reverse-contained", (1.0, 1.0, 3.0, 3.0), (0.0, 0.0, 4.0, 4.0)),
        ("identical", (0.0, 0.0, 2.0, 2.0), (0.0, 0.0, 2.0, 2.0)),
        ("edge-touch", (0.0, 0.0, 1.0, 1.0), (1.0, 0.0, 2.0, 1.0)),
        ("corner-touch", (0.0, 0.0, 1.0, 1.0), (1.0, 1.0, 2.0, 2.0)),
        ("disjoint", (0.0, 0.0, 1.0, 1.0), (2.0, 0.0, 3.0, 1.0)),
        ("survey", (512345.0, 4512345.0, 512349.0, 4512349.0),
         (512347.0, 4512346.0, 512351.0, 4512350.0)),
    )
    for label, left, right in layouts:
        for operation in range(3):
            yield f"{label}-operation-{operation}", encoded(
                operation, left, right, 1e-6 if label == "survey" else 1e-9,
            )

    curved = (
        ("circle-disjoint", 1, (0.0, 0.0, 1.0, 0.0), 1, (4.0, 0.0, 1.0, 0.0)),
        ("ellipse-disjoint", 2, (0.0, 0.0, 2.0, 1.0), 2, (6.0, 0.0, 2.0, 1.0)),
        ("hole-crossing", 3, (0.0, 0.0, 6.0, 6.0), 0, (3.0, -1.0, 7.0, 7.0)),
        ("hole-island", 3, (0.0, 0.0, 6.0, 6.0), 0, (2.0, 2.0, 4.0, 4.0)),
    )
    for label, left_kind, left, right_kind, right in curved:
        for operation in range(3):
            yield f"{label}-operation-{operation}", encoded(
                operation, left, right, left_kind=left_kind, right_kind=right_kind,
            )
    yield "circle-touch-operation-1", encoded(
        1, (0.0, 0.0, 2.0, 0.0), (4.0, 0.0, 2.0, 0.0),
        left_kind=1, right_kind=1,
    )
    yield "mixed-curves-refusal-operation-0", encoded(
        0, (-2.0, -1.0, 2.0, 1.0), (1.5, 0.0, 1.0, 0.0),
        left_kind=0, right_kind=1,
    )
    for operation in range(3):
        yield f"noncoplanar-operation-{operation}", encoded(
            operation, (0.0, 0.0, 2.0, 2.0), (0.5, 0.5, 1.5, 1.5),
            right_z=0.25,
        )
    for index, tolerance in enumerate((0.0, -1.0, math.inf, math.nan)):
        yield f"invalid-tolerance-{index}", encoded(
            index % 3, (0.0, 0.0, 2.0, 2.0), (1.0, 0.0, 3.0, 2.0), tolerance,
        )
    rng = random.Random(0x51EE7)
    for index in range(24):
        scale = 10.0 ** rng.uniform(-3.0, 4.0)
        x = rng.uniform(-100.0, 100.0)
        y = rng.uniform(-100.0, 100.0)
        width = scale * rng.uniform(1.0, 4.0)
        height = scale * rng.uniform(1.0, 4.0)
        left = (x, y, x + width, y + height)
        right = (
            x + width * rng.uniform(0.1, 0.9),
            y + height * rng.uniform(0.1, 0.9),
            x + width * rng.uniform(1.0, 1.8),
            y + height * rng.uniform(1.0, 1.8),
        )
        yield f"random-{index}", encoded(index % 3, left, right, max(1e-9, scale * 1e-8))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--compiler", type=Path, default=ROOT / "build/dynlex.exe")
    parser.add_argument("--rustc", type=Path, default=Path.home() / ".rustup/toolchains/stable-x86_64-pc-windows-msvc/bin/rustc.exe")
    parser.add_argument("--dependencies", type=Path, default=ROOT / "build/topology-reference-deps/target/debug/deps")
    parser.add_argument("--libraries", type=Path, default=ROOT / "build/brep-make-six-checks")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--case", default="")
    parser.add_argument("--optimization", action="append", choices=("O0", "O2"))
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()

    source = args.source.resolve()
    revision, _ = process(["git", "-c", f"safe.directory={source.as_posix()}", "-C", source, "rev-parse", "HEAD"])
    if revision != PIN:
        raise AssertionError(f"source revision {revision} != {PIN}")
    source_test_names = re.findall(
        r"#\[test\]\s*fn (\w+)",
        (source / "src/brep/presspull.rs").read_text(encoding="utf-8"),
    )
    expected_test_names = (HERE / "source-tests.txt").read_text(encoding="utf-8").splitlines()
    if source_test_names != expected_test_names or len(source_test_names) != 2:
        raise AssertionError("the pinned presspull.rs test inventory changed")
    source_files = [source / "src/brep/presspull.rs", source / "src/brep/boolean.rs",
                    source / "src/brep/sweep.rs"]
    process(["git", "-c", f"safe.directory={source.as_posix()}", "-C", source,
             "diff", "--exit-code", PIN, "--", *(path.relative_to(source) for path in source_files)])

    output = args.output or Path(tempfile.mkdtemp(prefix="brep-planar-", dir=ROOT / "build"))
    output.mkdir(parents=True, exist_ok=True)
    suffix = ".exe" if sys.platform == "win32" else ".out"
    modes = list(dict.fromkeys(args.optimization or ("O0", "O2")))
    compiler_hash = hashlib.sha256(args.compiler.read_bytes()).hexdigest()
    binaries = {}
    compile_times = {}
    if not args.skip_build:
        for mode in modes:
            reference = output / f"reference-{mode}{suffix}"
            native = output / f"native-{mode}{suffix}"
            diagnostics, rust_seconds = process([
                args.rustc, "--edition=2021", HERE / "planar_reference.rs",
                "--extern", f"cadkernel={(args.libraries / f'libcadkernel-{mode}.rlib').resolve()}",
                "-L", f"dependency={args.dependencies.resolve()}",
                "-C", f"opt-level={mode[1]}", "-o", reference,
            ])
            if diagnostics:
                raise AssertionError(f"unexpected Rust diagnostics: {diagnostics}")
            diagnostics, native_seconds = process([
                args.compiler.resolve(), HERE / "planar_probe.dl", f"-{mode}", "-o", native,
            ])
            if diagnostics:
                raise AssertionError(f"unexpected native diagnostics: {diagnostics}")
            binaries[mode] = {"rust": reference, "native": native}
            compile_times[mode] = {"rust": rust_seconds, "native": native_seconds}
            print(f"{mode}: Rust={rust_seconds:.3f}s native={native_seconds:.3f}s", flush=True)
    else:
        for mode in modes:
            binaries[mode] = {
                "rust": output / f"reference-{mode}{suffix}",
                "native": output / f"native-{mode}{suffix}",
            }

    count = comparisons = 0
    exact_native_optimization_parity = True
    raw_by_case = {}
    for name, values in cases():
        if args.case not in name:
            continue
        arguments = [format(value, ".17g") for value in values]
        outputs = {}
        limits_by_mode = {}
        for mode in modes:
            rust_text, _ = process([binaries[mode]["rust"], *arguments], timeout=120)
            native_text, _ = process([binaries[mode]["native"], *arguments], timeout=120)
            rust_values, limits_by_mode[mode] = MEASURE["expected_values"](rust_text)
            outputs[(mode, "rust")] = rust_values
            outputs[(mode, "native")] = CURVE["parsed"](native_text)
            raw_by_case[(name, mode)] = native_text
        if len(modes) == 2 and raw_by_case[(name, modes[0])] != raw_by_case[(name, modes[1])]:
            exact_native_optimization_parity = False
        for mode in modes:
            actual = outputs[(mode, "native")]
            expected = outputs[(mode, "rust")]
            if len(actual) != len(expected):
                raise AssertionError(f"{name}/{mode}: {len(actual)} native fields != {len(expected)} Rust fields")
            for field, (left, right, limit) in enumerate(zip(actual, expected, limits_by_mode[mode])):
                if not CURVE["equal"](left, right, exact=limit < 0, absolute=max(limit, 5e-9)):
                    raise AssertionError(f"{name}/{mode} field {field}: native={left!r}, Rust={right!r}")
            comparisons += len(expected)
        count += 1
        if count % 12 == 0:
            print(f"{count} cases passed ({name})", flush=True)

    if not count or hashlib.sha256(args.compiler.read_bytes()).hexdigest() != compiler_hash:
        raise AssertionError("empty run or compiler changed during verification")
    summary = {
        "revision": PIN,
        "compiler_sha256": compiler_hash,
        "cases": count,
        "comparisons": comparisons,
        "case_filter": args.case,
        "exact_native_optimization_parity": exact_native_optimization_parity,
        "compile_seconds": compile_times,
    }
    name = "filtered-summary.json" if args.case else "summary.json"
    (output / name).write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: {count} cases / {comparisons} comparisons; artifacts={output}", flush=True)


if __name__ == "__main__":
    main()
