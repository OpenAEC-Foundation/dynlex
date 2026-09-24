#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Build and time one equivalent B-rep workload in DynLex and pinned Rust."""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import statistics
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts"))
from process_error_mode import unattended_child_processes

PIN = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"
HERE = ROOT / "tests/cadkernel/performance"


def run(command, *, timeout=240, check=True):
    started = time.perf_counter()
    with unattended_child_processes():
        result = subprocess.run(
            [str(part) for part in command], cwd=ROOT, capture_output=True,
            text=True, timeout=timeout,
        )
    elapsed = time.perf_counter() - started
    if check and result.returncode:
        raise AssertionError(
            f"exit {result.returncode}: {' '.join(map(str, command))}\n"
            f"{result.stdout}{result.stderr}"
        )
    return result, elapsed


def executable(name):
    return name.with_suffix(".exe" if os.name == "nt" else ".out")


def parse_checksum(text):
    fields = text.split()
    if len(fields) != 2:
        raise AssertionError(f"expected two checksum fields, got {text!r}")
    return int(fields[0]), float(fields[1])


def percentile95(values):
    ordered = sorted(values)
    return ordered[math.ceil(len(ordered) * 0.95) - 1]


def timing_summary(values, iterations):
    median = statistics.median(values)
    return {
        "samples_seconds": values,
        "minimum_seconds": min(values),
        "median_seconds": median,
        "p95_seconds": percentile95(values),
        "iterations_per_second": iterations / median,
    }


def markdown(report):
    lines = [
        "# CAD-kernel performance comparison",
        "",
        f"Measured on {report['generated_utc']} with {report['settings']['iterations']} "
        "solid constructions per process.",
        "",
        "The workload cycles through a cuboid, cylinder, sphere, cone, hexagonal "
        "pyramid and hexagonal frustum. Every result is topology-validated, its "
        "Euler characteristic and arena sizes enter an integer checksum, and its "
        "worst vertex gap and spatial bounds enter a floating-point checksum.",
        "",
        "This is an interim constructor workload. It does not cover unfinished "
        "kernel operations or measure GPU rendering speed.",
        "",
        "| Mode | Implementation | Compile time | Runtime median | Runtime p95 | Solids/s |",
        "| --- | --- | ---: | ---: | ---: | ---: |",
    ]
    for mode in ("O0", "O2"):
        data = report["results"][mode]
        for implementation, label in (("dynlex", "DynLex"), ("rust", "Pinned Rust")):
            runtime = data["runtime"][implementation]
            compile_time = data["compile_seconds"][implementation + "_total"]
            lines.append(
                f"| {mode} | {label} | {compile_time:.3f} s | "
                f"{runtime['median_seconds']:.6f} s | {runtime['p95_seconds']:.6f} s | "
                f"{runtime['iterations_per_second']:.1f} |"
            )
        lines.append(
            f"| {mode} | DynLex / Rust runtime | - | "
            f"**{data['dynlex_to_rust_median_ratio']:.3f}x** | - | - |"
        )
    lines += [
        "",
        f"Each executable had {report['settings']['warmups']} warm-up runs and "
        f"{report['settings']['measured_runs']} measured runs. Measured runs alternated "
        "implementation order and include process startup. Both implementations "
        "produced matching checksums before and throughout timing.",
        "",
        "Compilation is reported for context. DynLex compiles the benchmark and its "
        "imported kernel dependency graph in one command. The Rust total is a full "
        "feature-enabled kernel library build plus the small benchmark driver, so the "
        "compilation units differ and their times are not a like-for-like compiler score.",
        "",
        "## Environment",
        "",
        f"- OS: `{report['environment']['platform']}`",
        f"- Processor: `{report['environment']['processor'] or 'not reported by OS'}`",
        f"- DynLex compiler SHA256: `{report['dynlex']['sha256']}`",
        f"- Rust: `{report['rust']['version'].splitlines()[0]}`",
        f"- Pinned source revision: `{report['source_revision']}`",
        "",
        "Reproduce from the repository root:",
        "",
        "```powershell",
        "python -B tests/cadkernel/performance/benchmark.py --source <pinned-source-path>",
        "```",
        "",
        "The full sample arrays and compile phase timings are recorded in "
        "`docs/cadkernel-performance.json`.",
        "Dynedra rendering integration is documented separately in "
        "[the GPU integration note](cadkernel-gpu-integration.md).",
        "",
    ]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--compiler", type=Path, default=ROOT / "build/dynlex.exe")
    parser.add_argument(
        "--rustc", type=Path,
        default=Path.home() / ".rustup/toolchains/stable-x86_64-pc-windows-msvc/bin/rustc.exe",
    )
    parser.add_argument(
        "--dependencies", type=Path,
        default=ROOT / "build/topology-reference-deps/target/debug/deps",
    )
    parser.add_argument("--iterations", type=int, default=10_000)
    parser.add_argument("--warmups", type=int, default=3)
    parser.add_argument("--runs", type=int, default=11)
    parser.add_argument("--output", type=Path, default=ROOT / "build/cadkernel-performance")
    parser.add_argument("--json", type=Path, default=ROOT / "docs/cadkernel-performance.json")
    parser.add_argument("--report", type=Path, default=ROOT / "docs/cadkernel-performance.md")
    args = parser.parse_args()
    if args.iterations <= 0 or args.warmups < 0 or args.runs < 3:
        parser.error("iterations must be positive, warmups nonnegative and runs at least 3")

    source = args.source.resolve()
    compiler = args.compiler.resolve()
    dependencies = args.dependencies.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    revision, _ = run(["git", "-c", f"safe.directory={source.as_posix()}", "-C", source, "rev-parse", "HEAD"])
    if revision.stdout.strip() != PIN:
        raise AssertionError(f"source revision {revision.stdout.strip()} != pinned {PIN}")
    rust_version, _ = run([args.rustc, "-vV"])
    externs = []
    for name in ("spade", "rustc_hash"):
        matches = list(dependencies.glob(f"lib{name}-*.rlib"))
        if len(matches) != 1:
            raise AssertionError(f"expected one {name} dependency, found {matches}")
        externs += ["--extern", f"{name}={matches[0]}"]

    compile_results = {}
    binaries = {}
    suffix = ".exe" if os.name == "nt" else ".out"
    for mode in ("O0", "O2"):
        level = mode[1]
        dynlex_binary = output / f"brep-dynlex-{mode}{suffix}"
        rust_library = output / f"libcadkernel-{mode}.rlib"
        rust_binary = output / f"brep-rust-{mode}{suffix}"

        result, dynlex_time = run([
            compiler, HERE / "brep_workload.dl", f"-{mode}", "-o", dynlex_binary,
        ])
        if result.stdout.strip() or result.stderr.strip():
            raise AssertionError(f"unexpected DynLex diagnostics for {mode}: {result.stdout}{result.stderr}")
        _, rust_library_time = run([
            args.rustc, "--edition=2021", "--crate-name=cadkernel", source / "src/lib.rs",
            "--cfg", 'feature="geom2d"', "--cfg", 'feature="brep"',
            "-L", f"dependency={dependencies}", *externs, "-C", f"opt-level={level}",
            "--crate-type=rlib", "-o", rust_library,
        ])
        _, rust_driver_time = run([
            args.rustc, "--edition=2021", HERE / "brep_workload.rs",
            "--extern", f"cadkernel={rust_library}", "-L", f"dependency={dependencies}",
            "-C", f"opt-level={level}", "-o", rust_binary,
        ])
        compile_results[mode] = {
            "dynlex_total": dynlex_time,
            "rust_library": rust_library_time,
            "rust_driver": rust_driver_time,
            "rust_total": rust_library_time + rust_driver_time,
        }
        binaries[mode] = {"dynlex": dynlex_binary, "rust": rust_binary}
        print(
            f"{mode} compiled: DynLex {dynlex_time:.3f}s; "
            f"Rust {rust_library_time + rust_driver_time:.3f}s",
            flush=True,
        )

    results = {}
    for mode in ("O0", "O2"):
        expected = None
        for implementation in ("dynlex", "rust"):
            result, _ = run([binaries[mode][implementation], args.iterations])
            checksum = parse_checksum(result.stdout)
            if expected is None:
                expected = checksum
            elif checksum[0] != expected[0] or not math.isclose(
                checksum[1], expected[1], rel_tol=3e-12, abs_tol=1e-12
            ):
                raise AssertionError(f"{mode} checksum mismatch: {expected} != {checksum}")

        for warmup in range(args.warmups):
            order = ("dynlex", "rust") if warmup % 2 == 0 else ("rust", "dynlex")
            for implementation in order:
                result, _ = run([binaries[mode][implementation], args.iterations])
                if parse_checksum(result.stdout)[0] != expected[0]:
                    raise AssertionError(f"{mode}/{implementation} warm-up checksum changed")

        timings = {"dynlex": [], "rust": []}
        for sample in range(args.runs):
            order = ("dynlex", "rust") if sample % 2 == 0 else ("rust", "dynlex")
            for implementation in order:
                result, elapsed = run([binaries[mode][implementation], args.iterations])
                checksum = parse_checksum(result.stdout)
                if checksum[0] != expected[0] or not math.isclose(
                    checksum[1], expected[1], rel_tol=3e-12, abs_tol=1e-12
                ):
                    raise AssertionError(f"{mode}/{implementation} measured checksum changed")
                timings[implementation].append(elapsed)
        runtime = {
            name: timing_summary(values, args.iterations)
            for name, values in timings.items()
        }
        results[mode] = {
            "checksum": {"discrete": expected[0], "metric": expected[1]},
            "compile_seconds": compile_results[mode],
            "runtime": runtime,
            "dynlex_to_rust_median_ratio": (
                runtime["dynlex"]["median_seconds"] / runtime["rust"]["median_seconds"]
            ),
        }
        print(
            f"{mode}: DynLex {runtime['dynlex']['median_seconds']:.6f}s; "
            f"Rust {runtime['rust']['median_seconds']:.6f}s; "
            f"ratio {results[mode]['dynlex_to_rust_median_ratio']:.3f}x",
            flush=True,
        )

    commit, _ = run(["git", "rev-parse", "HEAD"])
    report = {
        "schema_version": 1,
        "generated_utc": dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat(),
        "workload": "six B-rep constructors plus topology validation, vertex gap and bounds",
        "source_revision": PIN,
        "settings": {
            "iterations": args.iterations,
            "warmups": args.warmups,
            "measured_runs": args.runs,
            "includes_process_startup": True,
            "alternating_order": True,
        },
        "environment": {
            "platform": platform.platform(),
            "processor": platform.processor(),
            "python": platform.python_version(),
        },
        "dynlex": {
            "repository_commit": commit.stdout.strip(),
            "sha256": hashlib.sha256(compiler.read_bytes()).hexdigest(),
        },
        "rust": {"version": rust_version.stdout.strip()},
        "results": results,
    }
    args.json.parent.mkdir(parents=True, exist_ok=True)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    args.report.write_text(markdown(report), encoding="utf-8")
    print(f"Wrote {args.report} and {args.json}", flush=True)


if __name__ == "__main__":
    main()
