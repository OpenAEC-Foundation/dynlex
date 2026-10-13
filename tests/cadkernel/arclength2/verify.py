# SPDX-License-Identifier: MPL-2.0
"""Verify planar measurement against unchanged pinned Rust and native O0/O2."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import random
import re
import runpy
import shutil
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[3]
COMMON = runpy.run_path(str(ROOT / "tests/cadkernel/verify.py"))
CURVE = runpy.run_path(str(ROOT / "tests/cadkernel/curve2/verify.py"))
case = CURVE["case"]
PIN = CURVE["PIN"]


def run(command, *, check=True, timeout=60):
    status, output, seconds = COMMON["run_process"](
        [str(x) for x in command], timeout=timeout, cwd=ROOT)
    if check and status:
        raise AssertionError(f"exit {status}: {command}\n{output}")
    return status, output, seconds


def compile_program(command):
    _, output, seconds = run(command)
    if Path(command[0]).name.lower() in ("dynlex", "dynlex.exe") and output.strip():
        raise AssertionError("unexpected compiler diagnostics: " + output)
    print(f"compile {Path(command[-1]).name}: {seconds:.3f}s", flush=True)
    return seconds


def rust_driver(source, directory, module):
    # Reuse the checked dependency assembly and verbatim Ellipse extraction.
    driver = CURVE["rust_driver"](source, directory)
    text = driver.read_text(encoding="utf-8")
    old = f'include!(r"{(ROOT / "tests/cadkernel/curve2/reference.rs").as_posix()}");'
    text = text.replace(old, "\n".join(
        f'include!(r"{(ROOT / path).as_posix()}");' for path in (
            "tests/cadkernel/arclength2/input.rs",
            f"tests/cadkernel/{module}2/reference.rs")))
    driver.write_text(text, encoding="utf-8")
    return driver


def common_cases():
    for kind in range(8):
        yield f"variant-{kind}", case(kind, flags=1)
    for kind in (0, 1, 2, 3, 6, 7):
        for value in (-0., 0., 1e-200, 1e200, math.inf, -math.inf, math.nan):
            yield f"coordinates-{kind}-{value!r}", case(
                kind, start=(value, value), end=(value, 0.), flags=0)
    for kind in (1, 2, 3):
        for radius in (-10., -0., 0., 1e-200, 1e200, math.inf, math.nan):
            yield f"radius-{kind}-{radius!r}", case(kind, radius=radius, flags=0)
    for kind in (2, 3):
        for first, last in ((0., 0.), (1., 1.), (0., math.tau), (1., -.4),
                            (14., -14.), (-50., 70.), (math.nan, 0.), (0., math.inf)):
            yield f"sweep-{kind}-{first}-{last}", case(
                kind, first=first, last=last, flags=0)
    polylines = [[], [(2., 3., 0.)], [(0., 0., 1.)],
                 [(0., 0., 0.), (0., 0., 0.)],
                 [(0., 0., 1.), (0., 0., 0.)],
                 [(0., 0., 0.), (3., 0., 0.), (3., 4., 0.)],
                 [(0., 0., 1.), (2., 0., -1.)],
                 [(-1., 0., -1.), (1., 0., 0.)],
                 [(0., 0., 0.), (0., 0., 0.), (2., 0., 0.), (2., 0., 0.)]]
    for i, vertices in enumerate(polylines):
        for closed in (False, True):
            yield f"polyline-{i}-{closed}", case(4, vertices=vertices, closed=closed)
    for bulge in (-math.inf, -1e300, -2., -1., -1e-12, -0., 0.,
                  math.nextafter(1e-12, 0.), 1e-12, .4, 1., 2., 1e300, math.inf, math.nan):
        for chord in (0., math.nextafter(1e-12, 0.), 1e-12, 2.):
            yield f"bulge-{bulge!r}-{chord}", case(4,
                vertices=[(0., 0., bulge), (chord, 0., 0.)], flags=0)
    for degree in (1, 2, 3, 16, 17, 24):
        vertices = [(float(i), math.sin(i), 0.) for i in range(degree + 3)]
        yield f"nurbs-degree-{degree}", case(5, vertices=vertices, degree=degree,
            knots=CURVE["clamped"](degree, len(vertices)),
            weights=[1. + (i % 3) * .3 for i in range(len(vertices))])
    for knot in (0., 1e-12, math.nextafter(1e-12, math.inf), .5,
                 1. - 1e-12, math.nextafter(1. - 1e-12, 0.), 1.):
        yield f"nurbs-boundary-{knot}", case(5, knots=[0., 0., 0., knot, 1., 1., 1.])
    for knots in ((-2., -2., -2., 3., 6., 6., 6.), (0.,) * 7):
        yield f"nurbs-domain-{knots}", case(5, knots=knots, flags=0)
    for axis in ((0., 0.), (-1., 0.), (0., 1.), (.6, .8), (2., 3.)):
        yield f"ellipse-axis-{axis}", case(3, axis=axis, flags=0)
    rng = random.Random(953546)
    for i in range(80):
        vertices = [(rng.uniform(-10, 10), rng.uniform(-10, 10), rng.uniform(-2, 2))
                    for _ in range(rng.randint(3, 7))]
        yield f"random-{i}", case(i % 8, t=rng.uniform(-.5, 1.5),
            target=(rng.uniform(-4, 20), 0.), start=(rng.uniform(-2, 2), rng.uniform(-2, 2)),
            end=(rng.uniform(-2, 2), rng.uniform(-2, 2)), vertices=vertices,
            closed=bool(i % 2), weights=[rng.uniform(.1, 4.) for _ in vertices],
            radius=rng.uniform(.1, 5.), minor=rng.uniform(.1, 4.),
            first=rng.uniform(0., .5), last=rng.uniform(.5, 1.), density=.03, flags=1)


def cases(module):
    yield from common_cases()
    if module == "arclength":
        for kind in range(8):
            for t in (-math.inf, -2., -1., -0., 0., .5, 1., 2., math.inf, math.nan):
                yield f"parameter-{kind}-{t!r}", case(kind, t=t, flags=0)
            for distance in (-math.inf, -1., -0., 0., 1e-200, .5, 1., 30., 1e200, math.inf, math.nan):
                # NaN on a nondegenerate polyline is a source panic, checked separately.
                if kind == 4 and math.isnan(distance):
                    continue
                yield f"distance-{kind}-{distance!r}", case(kind, target=(distance, 0.), flags=0)
        for distance in (math.nextafter(3., 0.), 3., math.nextafter(3., math.inf), 7.):
            yield f"cumulative-boundary-{distance}", case(4, target=(distance, 0.),
                vertices=[(0., 0., 0.), (3., 0., 0.), (3., 0., 0.), (3., 4., 0.)], flags=0)
        for direction in ((0., -0.), (1e-200, 0.), (3., 4.), (math.inf, 0.), (math.nan, 0.)):
            for kind in (6, 7):
                yield f"unbounded-direction-{kind}-{direction}", case(kind, end=direction, flags=0)
    else:
        for kind in range(8):
            for tolerance in (-math.inf, -1., -0., 0., 1e-30, .001, .05, 1., 100., math.inf, math.nan):
                yield f"tolerance-{kind}-{tolerance!r}", case(kind, density=tolerance, flags=0)
            for angle in (-math.inf, -1., -0., 0., math.inf, math.nan, .05, math.pi):
                yield f"angle-{kind}-{angle!r}", case(kind, density=.03, angle=angle, flags=1)
        for kind in (1, 3):
            for tolerance in (.001, 0., math.inf, math.nan):
                yield f"cap-{kind}-{tolerance}", case(kind, radius=250000., minor=250000., density=tolerance, flags=0)
        yield "nurbs-depth-limit", case(5, density=0., flags=0,
            vertices=[(0., 0., 0.), (1., 2., 0.), (2., 0., 0.)], weights=[1., .7, 1.])
        yield "nurbs-s-bend", case(5, degree=3, density=.001, flags=1,
            vertices=[(0., 0., 0.), (1., 2., 0.), (2., -2., 0.), (3., 0., 0.)])
        yield "ellipse-angular-depth-limit", case(3, radius=0., minor=0., flags=1)
        for start, end in (((1e16, 0.), (1., 0.)), ((math.inf, 0.), (1., 0.)), ((-0., -0.), (0., 0.))):
            yield f"stored-line-{start}-{end}", case(0, start=start, end=end, flags=1)


def expected_values(output):
    values, limits = [], []
    for line in output.splitlines():
        tag, *parts = line.split()
        if tag == "d":
            values.append(float(parts[0])); limits.append(-1.)
        elif tag == "e":
            values.extend(map(float, parts)); limits.extend([-1.] * len(parts))
        elif tag == "p":
            geometry, *pair = map(float, parts)
            scale = max([geometry, *[abs(x) for x in pair if math.isfinite(x)]])
            slack = 16 * math.ulp(scale) if math.isfinite(scale) else 0.
            values.extend(pair); limits.extend([slack] * len(pair))
        else:
            values.append(float(line)); limits.append(0.)
    return values, limits


def equal(actual, expected, *, exact=False, absolute=0.):
    if math.isnan(expected):
        return math.isnan(actual)
    if math.isnan(actual):
        return False
    if actual == 0. or expected == 0.:
        return actual == expected and math.copysign(1., actual) == math.copysign(1., expected)
    return CURVE["equal"](actual, expected, exact, absolute)


def main(module="arclength"):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--compiler", type=Path, default=ROOT / "build" / ("dynlex.exe" if os.name == "nt" else "dynlex"))
    parser.add_argument("--rustc", type=Path)
    parser.add_argument("--case", default="")
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--rust-only", action="store_true")
    args = parser.parse_args()
    compiler_digest = hashlib.sha256(args.compiler.read_bytes()).hexdigest()
    directory = ROOT / f"build/cadkernel-{module}2-checks"
    directory.mkdir(exist_ok=True)
    source = args.source.resolve()
    driver = rust_driver(source, directory, module)
    fixture = ROOT / f"tests/cadkernel/{module}2"
    names = re.findall(r"#\[test\]\s*fn (\w+)", (source / f"src/geom2d/{module}.rs").read_text(encoding="utf-8"))
    expected = (fixture / "expected.txt").read_text(encoding="utf-8")
    if names != expected.splitlines() or len(names) != {"arclength": 14, "deviation": 16}[module]:
        raise AssertionError("source test mapping mismatch")
    rustc = args.rustc
    if rustc is None:
        rustc = (Path.home() / ".rustup/toolchains/stable-x86_64-pc-windows-msvc/bin/rustc.exe"
                 if os.name == "nt" else Path(shutil.which("rustc") or "rustc"))
    _, version, _ = run([rustc, "-vV"])
    if os.name == "nt" and "host: x86_64-pc-windows-msvc" not in version:
        raise AssertionError("MSVC Rust is required to match the native math target")
    references = {level: directory / f"reference-{level}.out" for level in ("O0", "O2")}
    probes = {level: directory / f"probe-{level}.out" for level in ("O0", "O2")}
    compiles = {}
    if not args.skip_build:
        for level in ("O0", "O2"):
            opts = [rustc, "--edition=2021", "--crate-name", f"{module}2_reference", driver, "-C", f"opt-level={level[1]}", "-A", "dead_code", "-A", "unused_variables"]
            compile_program([*opts, "-o", references[level]])
            upstream = directory / f"upstream-{level}.out"
            compile_program([*opts, "--test", "-o", upstream])
            _, output, _ = run([upstream, f"geom2d::{module}::tests::"])
            if f"{len(names)} passed; 0 failed" not in output:
                raise AssertionError(output)
            (directory / f"upstream-{level}.txt").write_text(output, encoding="utf-8")
            print(f"Rust {level}: {len(names)} source tests passed", flush=True)
        if args.rust_only:
            return
        for level, probe in probes.items():
            binary = directory / f"main-{level}.out"
            compiles[f"main-{level}"] = compile_program([args.compiler, fixture / "main.dl", f"-{level}", "-o", binary])
            _, output, _ = run([binary])
            if COMMON["normalize_output"](output) != COMMON["normalize_output"](expected):
                raise AssertionError("translated source fixture mismatch: " + output)
            print(f"native {level}: {len(names)} source groups passed", flush=True)
            compiles[f"probe-{level}"] = compile_program([args.compiler, fixture / "probe.dl", f"-{level}", "-o", probe])
    count = fields = 0
    for name, data in cases(module):
        if args.case and args.case not in name:
            continue
        arguments = [str(x) for x in data]
        _, ref_output, _ = run([references["O0"], *arguments])
        want, limits = expected_values(ref_output)
        _, ref2_output, _ = run([references["O2"], *arguments])
        ref2, _ = expected_values(ref2_output)
        if len(want) != len(ref2) or any(not equal(a, b, exact=True) for a, b in zip(want, ref2)):
            raise AssertionError(f"{name}: Rust O0/O2 divergence; input={data}")
        results = []
        for level, probe in probes.items():
            _, output, _ = run([probe, *arguments])
            actual = CURVE["parsed"](output)
            if len(actual) != len(want):
                raise AssertionError(f"{name}/{level}: {len(actual)} fields != Rust {len(want)}; input={data}")
            for i, (a, b) in enumerate(zip(actual, want)):
                if not equal(a, b, exact=limits[i] < 0, absolute=max(0., limits[i])):
                    raise AssertionError(f"{name}/{level} field {i}: {a!r} != Rust {b!r}; input={data}")
            results.append(actual)
        for i, (a, b) in enumerate(zip(*results)):
            if not equal(a, b, exact=True):
                raise AssertionError(f"{name} field {i}: native O0 {a!r} != O2 {b!r}; input={data}")
        count += 1
        fields += 2 * len(want)
        if count % 25 == 0:
            print(f"{count} cases passed ({name})", flush=True)
    if not count:
        raise AssertionError("no matching cases")
    refusal_checks = 0
    if module == "arclength":
        data = case(4, target=(math.nan, 0.), flags=0,
                    vertices=[(0., 0., 0.), (3., 0., 0.), (3., 4., 0.)])
        for level in ("O0", "O2"):
            arguments = [str(x) for x in data]
            status, output, _ = run([references[level], *arguments], check=False)
            if status != 101 or "panicked at" not in output:
                raise AssertionError(f"Rust {level}: expected unordered-distance panic, got {status}: {output}")
            status, output, _ = run([probes[level], *arguments], check=False)
            if status != 134:
                raise AssertionError(f"native {level}: expected unordered-distance abort, got {status}: {output}")
            refusal_checks += 2
    if hashlib.sha256(args.compiler.read_bytes()).hexdigest() != compiler_digest:
        raise AssertionError("compiler changed during verification")
    evidence = {"module": module, "revision": PIN, "rustc": version.strip(),
                "cases": count, "rust_comparisons": fields, "case_filter": args.case,
                "refusal_checks": refusal_checks,
                "exact_native_optimization_parity": True, "exact_rust_optimization_parity": True,
                "compile_seconds": compiles,
                "compiler_sha256": compiler_digest}
    (directory / ("filtered-summary.json" if args.case else "summary.json")).write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    print(f"PASS {module}2: {count} cases, {fields} Rust comparisons; exact O0/O2 parity", flush=True)


if __name__ == "__main__":
    main()
