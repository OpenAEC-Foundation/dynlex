# SPDX-License-Identifier: MPL-2.0
"""Compare arena operation traces and run its ten unmodified upstream tests."""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
from pathlib import Path
import shutil
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location("cadkernel_fixture_runner", ROOT / "tests/cadkernel/verify.py")
fixtures = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixtures)
PIN = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"

def checked(command, timeout=45):
    status, output, elapsed = fixtures.run_process([str(item) for item in command], timeout=timeout, cwd=ROOT)
    if status != 0:
        raise AssertionError(f"Exit {status}: {command}\n{output}")
    return output, elapsed

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--compiler", type=Path, default=ROOT / "build/dynlex.exe")
    args = parser.parse_args()
    source = args.source.resolve()
    revision, _ = checked(["git", "-c", f"safe.directory={source.as_posix()}", "-C", source, "rev-parse", "HEAD"])
    assert revision.strip() == PIN, revision
    rustc = shutil.which("rustc")
    if rustc is None:
        raise RuntimeError("rustc is required")
    parent = ROOT / "build/cadkernel-arena-checks"
    parent.mkdir(parents=True, exist_ok=True)
    artifacts = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    module = source / "src/brep/arena.rs"
    tests = artifacts / "upstream.out"
    checked([rustc, "--edition=2024", "--test", module, "-O", "-o", tests])
    report, _ = checked([tests])
    print(report.strip(), flush=True)
    library = artifacts / "libcadkernel_arena_source.rlib"
    checked([rustc, "--edition=2024", "--crate-type=rlib", "--crate-name=cadkernel_arena_source", module, "-O", "-o", library])
    reference = artifacts / "reference.out"
    checked([rustc, "--edition=2024", ROOT / "tests/cadkernel/arena/reference.rs", "--extern", f"cadkernel_arena_source={library}", "-O", "-o", reference])
    expected, _ = checked([reference])
    expected = fixtures.normalize_output(expected)
    for mode in ("O0", "O2"):
        program = artifacts / f"probe-{mode}.out"
        diagnostics, elapsed = checked([args.compiler.resolve(), "tests/cadkernel/arena/probe.dl", f"-{mode}", "-o", program])
        if diagnostics.strip():
            raise AssertionError(diagnostics)
        actual, _ = checked([program])
        fixtures.compare(f"{mode} upstream operation trace", expected, fixtures.normalize_output(actual))
        print(f"{mode}: 400 operations; {len(expected.splitlines())} output lines match; compile={elapsed:.3f}s", flush=True)
    print(f"Compiler SHA256: {hashlib.sha256(args.compiler.read_bytes()).hexdigest()}")
    print(f"Artifacts: {artifacts}")

if __name__ == "__main__":
    main()
