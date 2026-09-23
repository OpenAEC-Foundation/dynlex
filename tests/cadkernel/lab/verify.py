#!/usr/bin/env python3
"""Verify Dynedra Lab's models and controls, optionally drawing native frames."""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tests/cadkernel"))
import verify as fixtures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--graphics", action="store_true",
                        help="also compile the interactive application and render the six original scenarios and eight solid wireframes")
    parser.add_argument("--compiler", type=Path,
                        default=ROOT / "build" / ("dynlex.exe" if sys.platform == "win32" else "dynlex"))
    args = parser.parse_args()
    compiler = args.compiler.resolve()
    output = Path(tempfile.mkdtemp(prefix="dynedra-lab-", dir=ROOT / "build"))
    digest = hashlib.sha256(compiler.read_bytes()).hexdigest()
    print(f"Compiler SHA256: {digest}; artifacts: {output}", flush=True)
    names = ["model", "state"]
    if args.graphics:
        names.insert(0, "graphics")
    for level in ("O0", "O2"):
        for name in names:
            print(f"Checking {name} {level} (compile budget: 600s)", flush=True)
            result = fixtures.verify_fixture(
                ROOT / "tests/cadkernel/lab" / name, level, compiler, output, 600, 60, False)
            print(f"{name} {level}: {result}", flush=True)
        if args.graphics:
            executable = output / f"dynedra-lab-{level}.out"
            status, diagnostics, elapsed = fixtures.run_process(
                [str(compiler), "examples/dynedra/main.dl", f"-{level}", "-o", str(executable)],
                timeout=600, cwd=ROOT)
            if status != 0 or diagnostics.strip():
                raise AssertionError(f"Interactive compile {level}: exit {status}\n{diagnostics}")
            print(f"Interactive application {level}: compiled in {elapsed:.3f}s", flush=True)
    if hashlib.sha256(compiler.read_bytes()).hexdigest() != digest:
        raise AssertionError("Compiler changed during Lab verification")
    print(f"All requested checks passed. Artifacts: {output}", flush=True)


if __name__ == "__main__":
    main()
