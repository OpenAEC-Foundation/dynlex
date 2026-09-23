#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Verify helix source tests, managed ownership and pinned Rust/O0/O2 parity."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import random
import re
import shutil
import sys

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
sys.dont_write_bytecode = True
sys.path.insert(0, str(HERE.parent))
from verify import FixtureFailure, run_process, verify_fixture

PIN = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"
MODULES = ["src/space/helix.rs", "src/space/nurbs.rs", "src/space/spline.rs",
           "src/space/vec.rs", "src/tessellation.rs"]


def run(command, timeout=60):
    status, output, elapsed = run_process([str(x) for x in command], timeout=timeout,
                                          cwd=ROOT, phase="helix verification")
    if status:
        raise FixtureFailure(f"exit {status}: {command}\n{output}")
    return output, elapsed


def helix(*, base=(1., 2., 3.), axis=(0., 0., 1.), start=(1., 0., 0.),
          r0=2., r1=4., height=5., turns=1.25, clockwise=False):
    return [0, *base, *axis, *start, r0, r1, height, turns, int(clockwise)]


def frame(*, base=(0., 0., 0.), axis=(0., 0., 1.), start=(2., 0., 3.), tangent=(0., 1., 0.)):
    return [1, *base, *axis, *start, *tangent]


def cases():
    for angle in (0., math.pi, math.tau, 1e15 * math.tau):
        yield f"runtime-trig-{angle}", [2, angle]
    for clockwise in (False, True):
        for r0, r1 in ((2., 2.), (0., 2.), (2., 0.), (1., 2.), (2., 1.),
                       (1., 1.0000000001), (1., 1.00000002)):
            for height, turns in ((5., 1.25), (-3., 0.2), (0., 0.), (0., 1.0)):
                yield f"radii-{r0}-{r1}-height-{height}-turns-{turns}-cw-{clockwise}", helix(
                    r0=r0, r1=r1, height=height, turns=turns, clockwise=clockwise)
    for name, data in (
        ("negative-base", helix(r0=-1.)), ("negative-top", helix(r1=-1.)),
        ("zero-radii", helix(r0=0., r1=0.)), ("negative-turns", helix(turns=-1.)),
        ("zero-turn-height", helix(turns=0.)),
        ("zero-turn-bad-axis", helix(turns=0., height=0., axis=(0., 0., 0.))),
        ("zero-turn-parallel-start", helix(turns=0., height=0., start=(0., 0., 1.))),
        ("parallel-start", helix(start=(0., 0., 2.))),
        ("zero-axis", helix(axis=(0., 0., 0.))), ("zero-start", helix(start=(0., 0., 0.))),
        ("huge-axis", helix(axis=(1e308, 0., 0.))),
        ("tiny-axis", helix(axis=(0., 0., 1e-200))),
        ("normalization-small-axis", helix(axis=(0., 0., 1e-150))),
        ("skew-frame", helix(axis=(1., 2., 3.), start=(-4., 7., 2.))),
        ("negative-axis", helix(axis=(0., 0., -8.), start=(3., 2., 9.))),
        ("negative-zero", helix(base=(-0., -0., -0.), r0=-0., r1=2., height=-0., turns=-0.)),
        ("overflow-point-zero-turn", helix(base=(1e308, 0., 0.), r0=1e308, height=0., turns=0.)),
        ("overflow-controls", helix(base=(1e308, 0., 0.), r0=1e308)),
        ("length-hypot", helix(r0=1., r1=1., height=1e308, turns=1.)),
        ("length-overflow", helix(r0=1e308, r1=1e308, turns=1.)),
        ("primitive-small-constant", helix(r0=1., r1=2., height=0., turns=1e15)),
        ("total-angle-overflow", helix(turns=1e308)),
        ("smallest-turn", helix(turns=5e-324)),
    ):
        yield name, data
    cap = 100000. / 6.
    for turns in (math.nextafter(1./6., 0.), 1./6., math.nextafter(1./6., 1.),
                  math.nextafter(cap, 0.), cap, math.nextafter(cap, math.inf), 16667., 1e15):
        yield f"segment-boundary-{turns}", helix(turns=turns)
    for position in range(1, 14):
        for special in (math.nan, math.inf, -math.inf):
            data = helix()
            data[position] = special
            yield f"nonfinite-{position}-{special}", data
    for radius in (0., 5e-324, 1e-200, 1e-13, math.nextafter(1e-12, 0.),
                   1e-12, math.nextafter(1e-12, math.inf), 2.):
        yield f"frame-radius-{radius}", frame(start=(radius, 0., 2.))
    for name, data in (
        ("frame-skew", frame(base=(1., 2., 3.), axis=(1., 2., 3.), start=(5., 6., 8.), tangent=(2., 1., 4.))),
        ("frame-zero-axis", frame(axis=(0., 0., 0.))),
        ("frame-overflow-length", frame(start=(1e308, 1e308, 0.))),
        ("frame-overflow-delta", frame(base=(-1e308, 0., 0.), start=(1e308, 0., 0.))),
        ("frame-tip-axial-tangent", frame(start=(0., 0., 2.), tangent=(0., 0., 4.))),
        ("frame-tip-zero-tangent", frame(start=(0., 0., 0.), tangent=(0., 0., 0.))),
        ("frame-regular-zero-tangent", frame(tangent=(0., 0., 0.))),
    ):
        yield name, data
    for position in range(1, 13):
        for special in (math.nan, math.inf, -math.inf):
            data = frame()
            data[position] = special
            yield f"frame-nonfinite-{position}-{special}", data
    rng = random.Random(953546)
    for index in range(80):
        vector = lambda: tuple(rng.uniform(-10., 10.) for _ in range(3))
        yield f"random-helix-{index}", helix(base=vector(), axis=vector(), start=vector(),
            r0=rng.uniform(0., 8.), r1=rng.uniform(0., 8.), height=rng.uniform(-20., 20.),
            turns=rng.uniform(0.01, 6.), clockwise=bool(index % 2))
        yield f"random-frame-{index}", frame(base=vector(), axis=vector(), start=vector(), tangent=vector())


def numbers(output):
    result = []
    for line in output.splitlines():
        vector = line.startswith("p ")
        tokens = line[2:].split() if vector else [line]
        if len(tokens) != (3 if vector else 1):
            raise AssertionError(f"invalid output record: {line}")
        values = [math.nan if re.fullmatch(r"[+-]?nan(?:\([\w]+\))?", x, re.I) else float(x) for x in tokens]
        result.append(values)
    return result


def equal(a, b, exact=False):
    if math.isnan(a) or math.isnan(b):
        return math.isnan(a) and math.isnan(b)
    if a == b:
        return a != 0. or math.copysign(1., a) == math.copysign(1., b)
    return not exact and math.isfinite(a) and math.isfinite(b) and math.isclose(a, b, rel_tol=3e-12, abs_tol=3e-12)


def compare(actual, expected, label, exact=False):
    if len(actual) != len(expected):
        raise AssertionError(f"{label}: {len(actual)} records != {len(expected)}")
    for index, (left, right) in enumerate(zip(actual, expected)):
        if len(left) != len(right):
            raise AssertionError(f"{label}, record {index}: different record types")
        for component, (a, b) in enumerate(zip(left, right)):
            if not equal(a, b, exact=exact):
                raise AssertionError(f"{label}, record {index}, component {component}: {a!r} != {b!r}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--compiler", type=Path, default=ROOT / "build" / ("dynlex.exe" if os.name == "nt" else "dynlex"))
    parser.add_argument("--rust-toolchain", default="stable-x86_64-pc-windows-msvc" if os.name == "nt" else None,
                        help="Rustup toolchain; on Windows use MSVC to match the native UCRT math runtime")
    parser.add_argument("--reference-only", action="store_true")
    parser.add_argument("--cases-only", action="store_true", help="run differential cases without the lifetime/required fixtures")
    args = parser.parse_args()
    source, compiler = args.source.resolve(), args.compiler.resolve()
    git = ["git", "-c", f"safe.directory={source.as_posix()}", "-C", source]
    revision, _ = run([*git, "rev-parse", "HEAD"])
    if revision.strip() != PIN:
        raise AssertionError(f"expected source {PIN}, got {revision.strip()}")
    run([*git, "diff", "--exit-code", "HEAD", "--", *MODULES])
    rustc = shutil.which("rustc")
    if not rustc:
        raise RuntimeError("rustc is required")
    rust_command = [rustc] + (["+" + args.rust_toolchain] if args.rust_toolchain else [])
    rust_version, _ = run([*rust_command, "-vV"])
    output = ROOT / "build/cadkernel-helix-checks"
    output.mkdir(parents=True, exist_ok=True)
    def module(name, path):
        return f'#[path = r"{path.as_posix()}"] pub mod {name};\n'
    driver = module("tessellation", source / MODULES[4]) + "mod space {\n"
    driver += module("vector", source / MODULES[3]) + "pub use vector::Vec3;\n"
    driver += module("spline", source / MODULES[2]) + module("nurbs", source / MODULES[1])
    driver += "pub use nurbs::NurbsCurve3;\n" + module("helix", source / MODULES[0]) + "}\n"
    driver += f'include!(r"{(HERE / "reference.rs").as_posix()}");\n'
    driver_path = output / "reference-driver.rs"
    driver_path.write_text(driver, encoding="utf-8")
    reference = output / "reference.out"
    upstream = output / "upstream-tests.out"
    for flags, target in (([], reference), (["--test"], upstream)):
        log, elapsed = run([*rust_command, "--edition=2021", "--crate-name", "helix_reference", "-A", "dead_code", driver_path, "-O", *flags, "-o", target])
        (output / (target.stem + "-compile.txt")).write_text(log, encoding="utf-8")
        print(f"compile {target.name}: {elapsed:.3f}s", flush=True)
    log, _ = run([upstream, "space::helix::tests::"])
    (output / "upstream-tests.txt").write_text(log, encoding="utf-8")
    if "2 passed; 0 failed" not in log:
        raise AssertionError(log)
    print(log, flush=True)
    probes = {}
    compiler_hash = None
    if not args.reference_only:
        compiler_hash = hashlib.sha256(compiler.read_bytes()).hexdigest()
        for level in ("O0", "O2"):
            if not args.cases_only:
                for name in ("cadkernel_helix", "cadkernel_helix_ownership"):
                    path = ROOT / "tests/required" / name
                    print(f"{path.name}/{level}: " + verify_fixture(path, level, compiler, output, 60, 60, False), flush=True)
            target = output / f"probe-{level}.out"
            log, elapsed = run([compiler, HERE / "probe.dl", f"-{level}", "-o", target])
            if log.strip():
                raise AssertionError(f"unexpected probe diagnostics: {log}")
            probes[level] = target
            print(f"compile probe-{level}: {elapsed:.3f}s", flush=True)
        if hashlib.sha256(compiler.read_bytes()).hexdigest() != compiler_hash:
            raise AssertionError("compiler changed while building helix probes; rerun with a stable binary")
    count = fields = 0
    manifest = []
    for name, values in cases():
        command_args = [str(x) for x in values]
        expected, _ = run([reference, *command_args])
        want = numbers(expected)
        observed = []
        for level, executable in probes.items():
            actual, _ = run([executable, *command_args])
            got = numbers(actual)
            (output / "last-case.json").write_text(json.dumps({"name": name, "input": command_args, "level":level, "rust":expected, "native":actual}), encoding="utf-8")
            compare(got, want, f"{name}/{level}")
            observed.append(got)
        if observed:
            compare(*observed, label=f"{name}/O0-O2", exact=True)
        field_count = sum(len(record) for record in want)
        manifest.append({"name":name, "input":command_args, "fields":field_count})
        count += 1
        fields += field_count
        if count % 50 == 0:
            print(f"{count} cases checked", flush=True)
    report = {"revision":PIN, "upstream_tests":2, "cases":count, "reference_fields":fields,
              "rust_comparisons":fields * len(probes), "exact_O0_O2":bool(probes),
              "fixtures_checked":bool(probes) and not args.cases_only,
              "rust_compiler":rust_version.strip(),
              "source_sha256":{path:hashlib.sha256((source/path).read_bytes()).hexdigest() for path in MODULES},
              "compiler_sha256":compiler_hash}
    (output / "cases.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2), flush=True)


if __name__ == "__main__":
    main()
