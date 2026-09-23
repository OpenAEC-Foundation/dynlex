#!/usr/bin/env python3
"""Verify the owned topology foundation against the complete pinned Rust crate."""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import re
import shutil
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
PIN = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location("cadkernel_fixture_runner", ROOT / "tests/cadkernel/verify.py")
fixtures = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixtures)


def run(command, timeout=60):
    status, output, seconds = fixtures.run_process([str(x) for x in command], timeout=timeout, cwd=ROOT)
    if status:
        raise AssertionError(f"exit {status}: {command}\n{output}")
    return fixtures.normalize_output(output), seconds


def cases():
    for scenario in range(60):
        yield scenario, 0.25
    special = [-0.0, 0.0, 5e-324, -1.0, 1.0, 1e-200, 1e154, 1e308, math.inf, -math.inf, math.nan]
    for scenario in (35, 36, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54):
        for value in special:
            yield scenario, value
    for scenario, boundary in ((47, 1e-15), (49, 1.0), (51, sys.float_info.epsilon)):
        for value in (math.nextafter(boundary, 0.0), boundary, math.nextafter(boundary, math.inf)):
            yield scenario, value
            yield scenario, -value


def parse_number(token):
    # C's printf can include an implementation-defined NaN payload (MSVC: ind).
    if re.fullmatch(r"[+-]?nan(?:\([a-zA-Z0-9_]+\))?", token, re.IGNORECASE):
        return math.nan
    return float(token)


def compare(actual, expected, *, exact=False):
    left, right = actual.splitlines(), expected.splitlines()
    if len(left) != len(right):
        raise AssertionError(f"line count {len(left)} != {len(right)}")
    for index, (a, e) in enumerate(zip(left, right), 1):
        if a.startswith("number ") and e.startswith("number "):
            av, ev = parse_number(a.split()[1]), parse_number(e.split()[1])
            if math.isnan(ev):
                equal = math.isnan(av)
            elif av == ev:
                equal = av != 0 or math.copysign(1.0, av) == math.copysign(1.0, ev)
            else:
                equal = not exact and av != 0 and ev != 0 and math.isfinite(av) and math.isfinite(ev) and math.isclose(av, ev, rel_tol=3e-13, abs_tol=0.0)
            if equal:
                continue
        elif a == e:
            continue
        raise AssertionError(f"line {index}: {a!r} != {e!r}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--compiler", type=Path, default=ROOT / "build/dynlex.exe")
    parser.add_argument("--rustc", type=Path, default=shutil.which("rustc"))
    parser.add_argument("--dependencies", type=Path, default=ROOT / "build/cadkernel-upstream-tests/debug/deps")
    parser.add_argument("--rust-only", action="store_true", help="Check Rust and structural oracles without claiming native verification.")
    args = parser.parse_args()
    source = args.source.resolve()
    revision, _ = run(["git", "-c", "core.excludesFile=", "-c", f"safe.directory={source.as_posix()}", "-C", source, "rev-parse", "HEAD"])
    if revision != PIN:
        raise AssertionError(f"incorrect source revision {revision}")
    dirty, _ = run(["git", "-c", "core.excludesFile=", "-c", f"safe.directory={source.as_posix()}", "-C", source, "diff", "--no-ext-diff", "HEAD", "--", "src", "Cargo.toml", "Cargo.lock"])
    if dirty:
        raise AssertionError(f"upstream source was modified:\n{dirty}")
    if not args.rustc:
        raise RuntimeError("rustc is required")
    parent = ROOT / "build/cadkernel-brep-topology-checks"
    parent.mkdir(parents=True, exist_ok=True)
    artifacts = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    deps = args.dependencies.resolve()
    common = [args.rustc, "--edition=2021", "--crate-name=cadkernel", source / "src/lib.rs", "--cfg", 'feature="geom2d"', "--cfg", 'feature="brep"', "-L", f"dependency={deps}"]
    for name in ("spade", "rustc_hash"):
        matches = list(deps.glob(f"lib{name}-*.rlib"))
        if len(matches) != 1:
            raise RuntimeError(f"expected one compatible {name} rlib in {deps}, found {len(matches)}")
        common += ["--extern", f"{name}={matches[0]}"]
    tests = artifacts / "upstream-tests.out"
    diagnostics, _ = run([*common, "--test", "-o", tests])
    (artifacts / "upstream-build.txt").write_text(diagnostics, encoding="utf-8")
    output, _ = run([tests, "brep::tests::", "--test-threads=1"])
    (artifacts / "upstream-tests.txt").write_text(output, encoding="utf-8")
    if "4 passed; 0 failed" not in output:
        raise AssertionError(output)
    print("4 unchanged upstream provenance tests passed", flush=True)
    library = artifacts / "libcadkernel.rlib"
    diagnostics, _ = run([*common, "--crate-type=rlib", "-o", library])
    (artifacts / "upstream-library.txt").write_text(diagnostics, encoding="utf-8")
    reference = artifacts / "reference.out"
    diagnostics, _ = run([args.rustc, "--edition=2021", HERE / "reference.rs", "--extern", f"cadkernel={library}", "-L", f"dependency={deps}", "-o", reference])
    if diagnostics:
        raise AssertionError(diagnostics)
    corpus = list(cases())
    expected = []
    for index, (scenario, scalar) in enumerate(corpus):
        output, _ = run([reference, scenario, repr(scalar)])
        expected.append(output)
        (artifacts / f"rust-{index:03}.txt").write_text(output, encoding="utf-8")
    baseline = expected[0].split("clone\n")[0]
    if not baseline.startswith("scenario 0\neuler 2\nnumber 0.00000000000000000e0\n") or not baseline.endswith("flaws 0\n"):
        raise AssertionError(f"the closed seam scaffold is inconsistent:\n{baseline}")
    # Independent source-contract assertions: these defects are intentionally not validated.
    for scenario in (32, 33):
        before = expected[scenario].split("clone\n")[0]
        if not before.endswith("flaws 0\n"):
            raise AssertionError(f"validator unexpectedly inspects roots/shell owner: {scenario}")
    kinds = set()
    messages = set()
    for output in expected:
        for line in output.splitlines():
            if len(line) > 2 and line[0].isdigit() and line[1] == " ":
                kinds.add(int(line[0]))
                if line[0] in "01":
                    messages.add(line[2:])
    if kinds != set(range(10)) or len(messages) != 12:
        raise AssertionError(f"incomplete flaw branches: {kinds}, {messages}")
    print(f"Rust: {len(corpus)} graph cases; all 10 flaw variants and 12 messages exercised", flush=True)
    report = {"revision": revision, "upstream_tests": 4, "graph_cases": len(corpus), "rust_trace_lines": sum(len(x.splitlines()) for x in expected), "native_verified": False, "artifacts": str(artifacts)}
    report["source_sha256"] = {name: hashlib.sha256((source / name).read_bytes()).hexdigest() for name in ("src/brep/mod.rs", "src/brep/topology.rs")}
    report["native_source_sha256"] = {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest() for path in [ROOT / "lib/cadkernel/brep_topology.dl", ROOT / "lib/cadkernel/brep.dl", *sorted(HERE.glob("*.dl"))]}
    (artifacts / "cases.json").write_text(json.dumps([[scenario, repr(value)] for scenario, value in corpus], indent=2) + "\n", encoding="utf-8")
    (artifacts / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if not args.rust_only:
        mode_outputs = []
        for mode in ("O0", "O2"):
            print(fixtures.verify_fixture(HERE, mode, args.compiler.resolve(), artifacts, 120, 30, False), flush=True)
            probe = artifacts / f"probe-{mode}.out"
            diagnostics, elapsed = run([args.compiler.resolve(), HERE / "probe.dl", f"-{mode}", "-o", probe], 120)
            if diagnostics:
                raise AssertionError(diagnostics)
            outputs = []
            for index, (scenario, scalar) in enumerate(corpus):
                actual, _ = run([probe, scenario, repr(scalar)])
                (artifacts / f"{mode}-{index:03}.txt").write_text(actual, encoding="utf-8")
                try:
                    compare(actual, expected[index])
                    if mode_outputs:
                        compare(actual, mode_outputs[0][index], exact=True)
                except AssertionError as error:
                    raise AssertionError(f"{mode} case {index} ({scenario}, {scalar}): {error}") from error
                outputs.append(actual)
            mode_outputs.append(outputs)
            print(f"{mode}: {len(corpus)} graphs match Rust; compilation {elapsed:.3f}s", flush=True)
        report.update(native_verified=True, compiler_sha256=hashlib.sha256(args.compiler.read_bytes()).hexdigest())
    (artifacts / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2), flush=True)


if __name__ == "__main__":
    main()
