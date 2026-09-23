#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Native endpoint-joining fixtures and differential checks against pinned Rust."""
from __future__ import annotations
import argparse
import copy
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import random
import shutil
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
PIN = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location("cadkernel_verifier", ROOT / "tests/cadkernel/verify.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


def run(command, timeout=60):
    status, output, elapsed = runner.run_process([str(x) for x in command], timeout=timeout, cwd=ROOT)
    if status:
        raise RuntimeError(f"exit {status}: {command}\n{output}")
    return output, elapsed


def case(name, a, b, fuzz=1., connector=2., kind=2, mode=0):
    return name, [mode, fuzz, connector, kind, len(a), len(b),
                  *(x for terminal in [*a, *b] for p in terminal for x in p)]


def cases():
    a = [[0., 0., 0.], [1., 0., 0.]]
    b = [[2., 2., 0.], [2., 1., 0.]]
    connector = [[3., 1., 0.], [3., 0., 0.]]
    for fuzz in (-math.inf, -1., -1e-300, -0., 0., math.nextafter(1., 0.), 1., math.nextafter(1., 2.), math.inf, math.nan):
        yield case(f"extension-bound-{fuzz}", [a], [b], fuzz)
        for kind in range(3):
            yield case(f"join-bound-{fuzz}-{kind}", [a], [b], fuzz, kind=kind, mode=2)
    for gap in (-math.inf, -1., -0., 0., math.nextafter(2., 0.), 2., math.nextafter(2., 3.), math.inf, math.nan):
        for kind in range(3):
            yield case(f"connector-bound-{gap}-{kind}", [a], [connector], 1., gap, kind, 2)
    # Each coordinate validation, including the fixed interior endpoints.
    for coordinate in range(12):
        for value in (math.nan, math.inf, -math.inf):
            pair = copy.deepcopy([a,b])
            pair[coordinate // 6][coordinate // 3 % 2][coordinate % 3] = value
            yield case(f"nonfinite-{coordinate}-{value}", [pair[0]], [pair[1]])
            yield case(f"nonfinite-add-{coordinate}-{value}", [pair[0]], [pair[1]], mode=2, kind=1)
    for exponent in (-300, -160, -150, -100, -12, -6, 0, 6, 12, 100, 150, 160, 300):
        scale = 10.**exponent
        scaled = [[[x * scale for x in p] for p in t] for t in [a,b]]
        yield case(f"scaled-{exponent}", [scaled[0]], [scaled[1]], scale)
    for origin in (1e6, 1e12, 1e15):
        shifted = [[[x + origin for x in p] for p in t] for t in [a,b]]
        yield case(f"survey-{origin}", [shifted[0]], [shifted[1]])
    for angle in (0., math.nextafter(1e-12, 0.), 1e-12, math.nextafter(1e-12, 1.), 1e-10):
        yield case(f"angular-cutoff-{angle}", [a], [[[0., angle, 0.], [1., 0., 0.]]], 0.)
    # Plane residual at and around the characteristic-span tolerance.
    for z in (0., 1e-12, 1e-10, 1e-9, 2.8e-9, 3e-9, 1e-8, .1):
        yield case(f"skew-{z}", [a], [[[2., 2., z], [2., 1., z]]])
    for y in (-2., -1., -0., math.nextafter(0., 1.), .5, 1., 2.):
        yield case(f"fixed-interior-{y}", [a], [[[2., y, 0.], [2., 1., 0.]]])
    for points in ([], [[0., -0.]], [[-2., 3.], [10., -4.]], [[1e308, -1e308]], [[math.nan, 0.]], [[0., math.inf]]):
        for fuzz in (-1., -0., 0., .05, 2., math.nan, math.inf):
            yield f"planar-scale-{points}-{fuzz}", [1, fuzz, 0, 0, len(points), 0, *(x for p in points for x in p)]
    for first, second in (([], []), ([a], []), ([], [b]), ([a,a], [connector,connector]),
                          ([a], [[[0., .1, 0.], [1., .1, 0.]], b])):
        for kind in range(3):
            yield case(f"selection-order-{first}-{second}-{kind}", first, second, kind=kind, mode=3)
    rng = random.Random(953546)
    for trial in range(30):
        joint = [rng.uniform(-10., 10.) for _ in range(3)]
        u, v = ([rng.uniform(-1., 1.) for _ in range(3)] for _ in range(2))
        # Independently construct non-axis-aligned terminals about a known joint.
        first = [[joint[i] - factor * u[i] for i in range(3)] for factor in (2., .5)]
        second = [[joint[i] - factor * v[i] for i in range(3)] for factor in (3., .25)]
        yield case(f"spatial-extension-{trial}", [first], [second], 2.)
        for kind in range(3):
            yield case(f"spatial-join-{trial}-{kind}", [first], [second], 2., 5., kind, 2)
            others = [[[rng.uniform(-5., 5.) for _ in range(3)] for _ in range(2)] for _ in range(2)]
            yield case(f"spatial-selection-{trial}-{kind}", [first, others[0]], [others[1], second], 2., 5., kind, 3)


def exact(a, b):
    return (math.isnan(a) and math.isnan(b)) or (a == b and (a != 0 or math.copysign(1., a) == math.copysign(1., b)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--compiler", type=Path, default=ROOT / "build" / ("dynlex.exe" if os.name == "nt" else "dynlex"))
    args = parser.parse_args()
    source, compiler = args.source.resolve(), args.compiler.resolve()
    rustc = shutil.which("rustc")
    if not rustc:
        raise RuntimeError("rustc is required")
    git = ["git", "-c", f"safe.directory={source.as_posix()}", "-C", source]
    revision, _ = run([*git, "rev-parse", "HEAD"])
    if revision.strip() != PIN:
        raise AssertionError(f"expected source {PIN}, got {revision.strip()}")
    modules = {"vec": "src/space/vec.rs", "plane": "src/space/plane.rs", "endpoint_join": "src/space/endpoint_join.rs"}
    run([*git, "diff", "--exit-code", "HEAD", "--", *modules.values()])
    if (source / modules["endpoint_join"]).read_text(encoding="utf-8").count("#[test]") != 8:
        raise AssertionError("expected eight original endpoint tests")
    directory = Path(tempfile.mkdtemp(prefix="endpoint-join-verify-", dir=ROOT / "build"))
    print(f"Artifacts: {directory}", flush=True)
    def module(name):
        return f'#[path = r"{(source / modules[name]).as_posix()}"] pub mod {name};\n'
    driver = directory / "reference-driver.rs"
    driver.write_text("mod space {\n" + module("vec") + "pub use vec::Vec3;\n" +
                      module("plane") + "pub use plane::coplanarity_tolerance;\n" + module("endpoint_join") +
                      "}\n" + f'include!(r"{(HERE / "reference.rs").as_posix()}");\n', encoding="utf-8")
    tests, reference = directory / "upstream.out", directory / "reference.out"
    for extra, output in ((["--test"], tests), ([], reference)):
        diagnostics, _ = run([rustc, "--edition=2021", "--crate-name", "endpoint_join_reference", "-A", "dead_code", driver, *extra, "-O", "-o", output])
        (directory / f"{output.stem}-build.log").write_text(diagnostics, encoding="utf-8")
    output, _ = run([tests, "space::endpoint_join::tests::"])
    (directory / "upstream-tests.log").write_text(output, encoding="utf-8")
    if "8 passed; 0 failed" not in output:
        raise AssertionError(output)
    print("Pinned Rust: 8/8 original tests passed", flush=True)
    binaries, fixture_results = {}, {}
    fixture = HERE if (HERE / "main.dl").is_file() else ROOT / "tests/required/cadkernel_endpoint_join3"
    for level in ("O0", "O2"):
        detail = runner.verify_fixture(fixture, level, compiler, directory, 60, 20, False)
        fixture_results[level] = detail
        print(f"Fixture {level}: {detail}", flush=True)
        binary = directory / f"probe-{level}.out"
        diagnostics, elapsed = run([compiler, (HERE / "probe.dl").relative_to(ROOT), f"-{level}", "-o", binary])
        (directory / f"probe-{level}-build.log").write_text(diagnostics, encoding="utf-8")
        if diagnostics.strip():
            raise AssertionError(diagnostics)
        print(f"Probe {level}: compiled in {elapsed:.3f}s", flush=True)
        binaries[level] = binary
    total = fields = 0
    evidence = []
    for name, data in cases():
        arguments = [str(x) for x in data]
        expected, _ = run([reference, *arguments], 10)
        wanted = [float(x) for x in expected.splitlines()]
        outputs = {}
        for level, binary in binaries.items():
            output, _ = run([binary, *arguments], 10)
            actual = [float(x) for x in output.splitlines()]
            outputs[level] = actual
            if len(actual) != len(wanted) or not all(exact(a,b) for a,b in zip(actual, wanted)):
                raise AssertionError(f"{name} {level}: {actual!r} != Rust {wanted!r}; arguments={arguments}")
            fields += len(wanted)
        if not all(exact(a,b) for a,b in zip(outputs["O0"], outputs["O2"])):
            raise AssertionError(f"{name}: O0/O2 parity")
        evidence.append({"case": name, "arguments": arguments, "rust": expected})
        total += 1
        if total % 50 == 0:
            print(f"Differential: {total} cases passed", flush=True)
    (directory / "reference-cases.json").write_text(json.dumps(evidence, indent=2), encoding="utf-8")
    result = {"cases": total, "fields": fields, "compiler_sha256": hashlib.sha256(compiler.read_bytes()).hexdigest(), "fixtures": fixture_results}
    (directory / "result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(f"PASS: {total} differential cases; {fields} exact Rust field comparisons; exact O0/O2 parity", flush=True)


if __name__ == "__main__":
    main()
