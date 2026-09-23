#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Differential checks for native B-rep tessellation and mesh properties."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import random
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
HERE = ROOT / "tests/cadkernel/brep_mesh"
PIN = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"
sys.path.insert(0, str(ROOT / "tests/cadkernel"))
from verify import run_process, verify_fixture


def process(command, timeout=240):
    status, output, elapsed = run_process(
        [str(part) for part in command], timeout=timeout, cwd=ROOT,
        phase="mesh differential verification",
    )
    if status:
        raise AssertionError(f"exit {status}: {command}\n{output}")
    return output, elapsed


def compile_clean(command, timeout=300):
    output, elapsed = process(command, timeout)
    if output.strip():
        raise AssertionError(f"unexpected compiler output: {command}\n{output}")
    return elapsed


def cases():
    default = math.pi / 24.0
    yield "empty", [3, 0, 0, 0, 0, 0, 0, default, 1e-9, 0]
    yield "box", [0, 0, 0, 0, 2, 3, 4, default, 1e-9, 0]
    yield "box-survey", [0, 512345.678, 4512345.678, 91.5, .5, .5, .5, default, 1e-6, 0]
    yield "cylinder-default", [1, 0, 0, 0, 5, 10, 0, default, 1e-9, 0]
    yield "cylinder-coarse", [1, -4, 2, 7, 2.5, 3, 0, 1.0, 1e-9, 0]
    yield "cylinder-fine", [1, 3, -2, 1, 1.25, 7, 0, .03, 1e-9, 0]
    yield "cylinder-chordal", [1, 0, 0, 0, 5, 10, 0, 1.0, 1e-9, .01]
    yield "cylinder-isolines", [1, 0, 0, 0, 3, 6, 0, .25, 1e-9, 0, 1, 1]
    yield "sphere", [2, 1, 2, 3, 4, 0, 0, default, 1e-9, 0]
    yield "sphere-shifted", [2, -8, 5, 11, 1.5, 0, 0, .2, 1e-9, 0]
    yield "sphere-chordal", [2, 0, 0, 0, 4, 0, 0, 1.0, 1e-9, .02]
    yield "sphere-isolines", [2, 0, 0, 0, 2, 0, 0, .25, 1e-9, 0, 1, 1]
    yield "cone", [4, 0, 0, 0, 3, 5, 0, default, 1e-9, 0]
    yield "cone-isolines", [4, 0, 0, 0, 3, 5, 0, .25, 1e-9, 0, 1, 1]
    yield "torus", [5, 0, 0, 0, 4, 1, 0, default, 1e-9, 0]
    yield "torus-isolines", [5, 2, -3, 5, 4, 1, 0, .25, 1e-9, 0, 1, 1]
    yield "elliptical-cone", [6, 0, 0, 0, 4, 2, 0, default, 1e-9, 0, 0, 0, 6]
    yield "elliptical-frustum", [6, 1, -2, 3, 4, 2, 2, default, 1e-9, 0, 0, 0, 6]
    yield "wedge", [7, 0, 0, 0, 4, 3, 5, default, 1e-9, 0]
    yield "pyramid", [8, 0, 0, 0, 4, 9, 6, default, 1e-9, 0]
    rng = random.Random(0x4D455348)
    for index in range(20):
        origin_scale = 1e8 if index % 7 == 0 else 100.0
        origin = [rng.uniform(-origin_scale, origin_scale) for _ in range(3)]
        if index % 3:
            size = [10 ** rng.uniform(-1, 2) for _ in range(3)]
            tolerance = max(1e-9, origin_scale * 5e-15)
            yield f"random-box-{index}", [0, *origin, *size, default, tolerance, 0]
        else:
            radius = 10 ** rng.uniform(-1, 1)
            height = 10 ** rng.uniform(-1, 2)
            angle = (default, .2, .07)[index % 3]
            tolerance = max(1e-9, origin_scale * 5e-15)
            yield f"random-cylinder-{index}", [1, *origin, radius, height, 0, angle, tolerance, 0]


def arguments(values):
    padded = [*values, *([0] * max(0, 13 - len(values)))]
    return [format(value, ".17g") if isinstance(value, float) else str(value) for value in padded]


def parsed(output):
    values = [float(line.strip()) for line in output.splitlines() if line.strip()]
    if len(values) != 41:
        raise AssertionError(f"expected 41 metrics, got {len(values)}\n{output}")
    return values


def near(actual, expected, *, relative=1e-10, absolute=1e-9):
    return math.isclose(actual, expected, rel_tol=relative, abs_tol=absolute)


def compare(name, shape, actual, expected):
    checks = 0
    topology_independent = shape in (2, 6)
    if not topology_independent:
        assert actual[0] == expected[0], f"{name}: triangles {actual[0]} != {expected[0]}"
        assert actual[1] == expected[1], f"{name}: positions {actual[1]} != {expected[1]}"
        checks += 2
    else:
        assert actual[0] > 0 and actual[1] == actual[0] * 3, f"{name}: mesh topology {actual[:2]}"
        checks += 2
    scale = max(1.0, *(abs(value) for value in expected[7:13]))
    surface_relative = 1e-3 if shape == 6 else 2e-10
    surface_absolute = 1e-3 if shape == 6 else 2e-9 * scale
    assert near(actual[2], expected[2], relative=surface_relative, absolute=surface_absolute), \
        f"{name}: area {actual[2]} != {expected[2]}"
    checks += 1
    for index in range(3, 6):
        centroid_absolute = 1e-3 if shape == 6 else 2e-6
        assert near(actual[index], expected[index], absolute=centroid_absolute), \
            f"{name}: surface centroid[{index - 3}] {actual[index]} != {expected[index]}"
        checks += 1
    if actual[0] > 0:
        winding_floor = .5 if shape == 2 else .9
        reference_winding = abs(expected[6]) if shape == 6 else expected[6]
        assert actual[6] > winding_floor and reference_winding > winding_floor, \
            f"{name}: winding {actual[6]}, {expected[6]}"
    else:
        assert actual[6] == expected[6] == 0.0
    checks += 1
    for index in range(7, 13):
        assert near(actual[index], expected[index], absolute=2e-6), \
            f"{name}: bound[{index - 7}] {actual[index]} != {expected[index]}"
        checks += 1
    if shape == 6:
        assert actual[13] > 0 and expected[13] > 0, f"{name}: empty normal sample set"
    else:
        assert actual[13] == expected[13], f"{name}: angle count {actual[13]} != {expected[13]}"
    assert actual[14] == expected[14], f"{name}: mass validity {actual[14]} != {expected[14]}"
    checks += 2
    for index in range(15, 22):
        if shape == 6:
            relative = 1e-3
            limit = 1e-3
        else:
            relative = 2e-8
            limit = 2e-6 if index in (16, 17, 18) else 2e-8 * max(1.0, abs(expected[index]))
        assert near(actual[index], expected[index], relative=relative, absolute=limit), \
            f"{name}: mass metric[{index}] {actual[index]} != {expected[index]}"
        checks += 1
    if not topology_independent:
        assert actual[22] == expected[22], f"{name}: display triangles {actual[22]} != {expected[22]}"
    else:
        assert actual[22] > 0, f"{name}: empty mesh display"
    assert actual[23] == actual[22], f"{name}: triangle provenance {actual[23]} != {actual[22]}"
    assert expected[23] == expected[22], f"{name}: reference provenance {expected[23]} != {expected[22]}"
    checks += 3
    for index, label in ((24, "display edges"), (26, "isolines"),
                         (27, "missing faces"), (28, "complete"), (29, "wire edges"),
                         (31, "wire isolines"), (32, "wire missing"),
                         (33, "silhouette points")):
        assert actual[index] == expected[index], f"{name}: {label} {actual[index]} != {expected[index]}"
        checks += 1
    if shape == 6:
        assert actual[25] >= actual[24] and expected[25] >= expected[24], f"{name}: incomplete display edges"
        assert actual[30] >= actual[29] and expected[30] >= expected[29], f"{name}: incomplete wire edges"
    else:
        assert actual[25] == expected[25], f"{name}: edge points {actual[25]} != {expected[25]}"
        assert actual[30] == expected[30], f"{name}: wire points {actual[30]} != {expected[30]}"
    checks += 2
    for index in range(34, 40):
        assert near(actual[index], expected[index], relative=2e-8, absolute=2e-6), \
            f"{name}: silhouette bound[{index - 34}] {actual[index]} != {expected[index]}"
        checks += 1
    assert near(actual[40], expected[40], relative=2e-12, absolute=1e-12), \
        f"{name}: silhouette precision {actual[40]} != {expected[40]}"
    checks += 1
    return checks


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
    assert revision.strip() == PIN, (revision, PIN)
    source_file = source / "src/brep/mesh.rs"
    process(["git", "-c", f"safe.directory={source.as_posix()}", "-C", source,
             "diff", "--exit-code", PIN, "--", source_file.relative_to(source)])

    output = args.output or Path(tempfile.mkdtemp(prefix="brep-mesh-", dir=ROOT / "build"))
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
            rust_time = compile_clean([
                args.rustc, "--edition=2021", HERE / "reference.rs",
                "--extern", f"cadkernel={(args.libraries / f'libcadkernel-{mode}.rlib').resolve()}",
                "-L", f"dependency={args.dependencies.resolve()}",
                "-C", f"opt-level={mode[1]}", "-o", reference,
            ])
            native_time = compile_clean([args.compiler.resolve(), HERE / "probe.dl", f"-{mode}", "-o", native])
            binaries[mode] = (reference, native)
            compile_times[mode] = {"rust": rust_time, "native": native_time}
            for fixture in (
                "cadkernel_brep_mesh",
                "cadkernel_brep_mesh_mass",
                "cadkernel_brep_mesh_periodic",
                "cadkernel_brep_mesh_periodic_isolines",
                "cadkernel_brep_mesh_hole",
                "cadkernel_brep_mesh_boolean",
                "cadkernel_brep_mesh_missing_faces",
                "cadkernel_brep_mesh_nurbs",
                "cadkernel_brep_mesh_chordal",
                "cadkernel_brep_mesh_ownership",
                "cadkernel_brep_mesh_wireframe",
                "cadkernel_brep_mesh_surfaces",
                "cadkernel_brep_mesh_silhouette",
                "cadkernel_brep_mesh_cone_silhouette",
            ):
                print(f"{fixture}/{mode}: " + verify_fixture(
                    ROOT / "tests/required" / fixture, mode, args.compiler.resolve(), output,
                    300, 60, False,
                ), flush=True)
    else:
        binaries = {mode: (output / f"reference-{mode}{suffix}", output / f"native-{mode}{suffix}") for mode in modes}

    selected = [(name, values) for name, values in cases() if args.case in name]
    comparisons = 0
    native_outputs = {}
    for index, (name, values) in enumerate(selected):
        argv = arguments(values)
        for mode in modes:
            reference_text, _ = process([binaries[mode][0], *argv], 120)
            native_text, _ = process([binaries[mode][1], *argv], 120)
            native_outputs[(name, mode)] = native_text
            comparisons += compare(f"{name}/{mode}", int(values[0]), parsed(native_text), parsed(reference_text))
        if len(modes) == 2:
            assert native_outputs[(name, modes[0])] == native_outputs[(name, modes[1])], f"{name}: native O0/O2 differ"
        if (index + 1) % 8 == 0:
            print(f"{index + 1}/{len(selected)} mesh cases passed", flush=True)

    assert selected and hashlib.sha256(args.compiler.read_bytes()).hexdigest() == compiler_hash
    report = {
        "revision": PIN,
        "compiler_sha256": compiler_hash,
        "source_tests": 17,
        "cases": len(selected),
        "comparisons": comparisons,
        "case_filter": args.case,
        "exact_native_optimization_parity": len(modes) < 2 or all(
            native_outputs[(name, modes[0])] == native_outputs[(name, modes[1])] for name, _ in selected
        ),
        "compile_seconds": compile_times,
        "source_sha256": {"src/brep/mesh.rs": hashlib.sha256(source_file.read_bytes()).hexdigest()},
        "native_source_sha256": {
            str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in (ROOT / "lib/cadkernel/brep_mesh.dl", HERE / "probe.dl", HERE / "reference.rs")
        },
    }
    summary = "filtered-summary.json" if args.case else "summary.json"
    (output / summary).write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: {len(selected)} cases / {comparisons} comparisons; artifacts={output}", flush=True)


if __name__ == "__main__":
    main()
