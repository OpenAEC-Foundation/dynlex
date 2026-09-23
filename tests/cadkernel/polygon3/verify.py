#!/usr/bin/env python3
"""Compare spatial ring measurements with the pinned Rust source."""
from __future__ import annotations
import argparse
import math
from pathlib import Path
import random
import re
import shutil
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tests/cadkernel"))
import verify as fixtures
PIN = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"

def run(command, timeout=30):
    status, output, elapsed = fixtures.run_process([str(v) for v in command], timeout=timeout, cwd=ROOT)
    if status:
        raise AssertionError(f"exit {status}: {command}\n{output}")
    return output

def compile_program(command):
    output = run(command, 60)
    if output.strip():
        raise AssertionError(f"unexpected compiler diagnostics:\n{output}")

def same(actual, expected, exact=False):
    if math.isnan(expected):
        return math.isnan(actual)
    if actual == expected:
        return actual != 0.0 or math.copysign(1.0, actual) == math.copysign(1.0, expected)
    return not exact and math.isfinite(actual) and math.isfinite(expected) and math.isclose(actual, expected, rel_tol=3e-13, abs_tol=1e-300)

def parse_number(text):
    # Windows printf may include a NaN classification in parentheses.
    if re.fullmatch(r"[+-]?nan(?:\([a-zA-Z0-9_]+\))?", text, re.IGNORECASE):
        return math.nan
    return float(text)

def cases():
    square = [[0.0,0.0,0.0],[10.0,0.0,0.0],[10.0,10.0,0.0],[0.0,10.0,0.0]]
    for count in range(5):
        yield f"prefix {count}", square[:count]
    for scale in [0.0,-0.0,5e-324,1e-200,1e-100,1.0,1e100,1e154,1e308]:
        ring = [[value * scale for value in point] for point in square]
        yield f"scale {scale}", ring
        yield f"reverse scale {scale}", list(reversed(ring))
    for special in [math.inf,-math.inf,math.nan]:
        for axis in range(3):
            ring = [point.copy() for point in square]
            ring[2][axis] = special
            yield f"nonfinite {special} axis {axis}", ring
    random_source = random.Random(89311)
    for index in range(180):
        count = random_source.randrange(0, 65)
        origin = [random_source.uniform(-1e6,1e6) for _ in range(3)] if index % 3 == 0 else [0.0]*3
        ring = [[origin[axis]+random_source.uniform(-20,20) for axis in range(3)] for _ in range(count)]
        if ring and index % 4 == 0:
            ring.append(ring[0].copy())
        if ring and index % 5 == 0:
            ring.insert(0,ring[0].copy())
        yield f"random {index}", ring

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source",required=True,type=Path)
    parser.add_argument("--compiler",type=Path,default=ROOT/"build"/("dynlex.exe" if sys.platform=="win32" else "dynlex"))
    args=parser.parse_args()
    source=args.source.resolve()
    git=["git","-c",f"safe.directory={source.as_posix()}","-C",source]
    assert run([*git,"rev-parse","HEAD"]).strip()==PIN
    run([*git,"diff","--exit-code","HEAD","--","src/space/polygon.rs","src/space/vec.rs"])
    directory=ROOT/"build/cadkernel-polygon3-checks"
    directory.mkdir(parents=True,exist_ok=True)
    driver=directory/"reference-driver.rs"
    driver.write_text(
        '#![allow(dead_code)]\nmod space {\n'
        f'#[path=r"{(source/"src/space/vec.rs").as_posix()}"] pub mod vec;\n'
        f'#[path=r"{(source/"src/space/polygon.rs").as_posix()}"] pub mod polygon;\n'
        '}\n'
        f'include!(r"{(Path(__file__).parent/"reference.rs").as_posix()}");\n',encoding="utf-8")
    rustc=shutil.which("rustc") or Path.home()/".cargo/bin/rustc.exe"
    reference=directory/"reference.out"
    compile_program([rustc,"--edition=2021","--crate-name","polygon_reference",driver,"-O","-o",reference])
    executables=[reference]
    for level in ("O0","O2"):
        print(fixtures.verify_fixture(ROOT/"tests/required/cadkernel_polygon3",level,args.compiler.resolve(),directory,30,15,False),flush=True)
        probe=directory/f"probe-{level}.out"
        compile_program([args.compiler.resolve(),Path(__file__).parent/"probe.dl",f"-{level}","-o",probe])
        executables.append(probe)
    count=comparisons=0
    for label,ring in cases():
        values=[str(len(ring)),*(str(v) for point in ring for v in point)]
        outputs=[[parse_number(v) for v in run([program,*values]).splitlines()] for program in executables]
        expected,unoptimized,optimized=outputs
        assert len(expected)==len(unoptimized)==len(optimized),(label,[len(v) for v in outputs])
        for field,wanted in enumerate(expected):
            for actual in (unoptimized[field],optimized[field]):
                assert same(actual,wanted,exact=field==4),(label,field,wanted,actual)
                comparisons+=1
            assert same(unoptimized[field],optimized[field],exact=True),(label,"optimization mismatch",field)
        count+=1
        if count%50==0: print(f"{count} cases passed",flush=True)
    print(f"{count} cases, {comparisons} Rust comparisons passed; exact O0/O2 numerical parity",flush=True)

if __name__=="__main__":
    main()
