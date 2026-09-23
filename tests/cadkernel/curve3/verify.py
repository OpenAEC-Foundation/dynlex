#!/usr/bin/env python3
"""Verify every spatial curve helper against the pinned Rust module."""
from __future__ import annotations
import argparse
import math
from pathlib import Path
import random
import re
import shutil
import sys

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/"tests/cadkernel"))
import verify as fixtures
PIN="953d546b68aef4b6692566a1a9b077fc5bd9fb4f"

def run(command,timeout=30):
    status,output,_=fixtures.run_process([str(v) for v in command],timeout=timeout,cwd=ROOT)
    if status: raise AssertionError(f"exit {status}: {command}\n{output}")
    return output

def compile_program(command):
    output=run(command,60)
    if output.strip(): raise AssertionError(f"unexpected compiler diagnostics:\n{output}")

def parse_number(text):
    if re.fullmatch(r"[+-]?nan(?:\([a-zA-Z0-9_]+\))?",text,re.IGNORECASE): return math.nan
    return float(text)

def same(actual,expected,exact=False):
    if math.isnan(expected): return math.isnan(actual)
    if actual==expected: return actual!=0.0 or math.copysign(1.0,actual)==math.copysign(1.0,expected)
    return not exact and math.isfinite(actual) and math.isfinite(expected) and math.isclose(actual,expected,rel_tol=3e-13,abs_tol=1e-300)

def cases():
    random_source=random.Random(82111)
    for count in (0,1,2,3,4,8,17):
        control=[random_source.uniform(-10,10) for _ in range(count*3)]
        for parameter in (-2.0,-0.0,0.0,0.37,1.0,2.0,math.nan,math.inf):
            yield f"bezier {count} at {parameter}",[0,count,parameter,0 if count%2==0 else 8,*control]
    for index in range(80):
        count=random_source.randrange(0,20)
        yield f"random bezier {index}",[0,count,random_source.uniform(-1,2),random_source.randrange(0,16),*[random_source.uniform(-1e6,1e6) for _ in range(count*3)]]
    basics=[
        [1,0,0,0,1,0,0,0,1],
        [1,1e-9,0,-1,1e-9,0,0,0,1],
        [1,0,0,-1,0,0,0,0,1],
        [1,0,0,-1,0,0,1,0,0],
        [0,0,0,0,0,0,0,0,0],
        [1.7976931348623157e308,1.7976931348623157e308,0]*2+[0,0,1],
    ]
    for index,value in enumerate(basics): yield f"curvature bisector basic {index}",[1,*value]
    for scale in (0.0,5e-324,1e-200,1e-154,1e-100,1.0,1e100,1e154,1e308):
        yield f"scaled curvature {scale}",[1,scale,0,0,0,scale,0,0,0,scale]
    for nonfinite in (math.nan,math.inf,-math.inf):
        for axis in range(9):
            points=basics[0].copy()
            points[axis]=nonfinite
            yield f"nonfinite axis {axis} {nonfinite}",[1,*points]
    for index in range(100):
        yield f"random curvature {index}",[1,*[random_source.uniform(-1e6,1e6) for _ in range(9)]]
    for tolerance in (-1.0,-0.0,0.0,5e-324,1e-12,1e-7,0.1,1.0,math.inf,math.nan):
        for start,end,offset in [(0,2,0),(1,3,0),(2,3,0),(3,4,0),(1,1.5,0),(2,0,0),(0,2,1e-8),(0,2,1.0)]:
            yield f"segment tolerance {tolerance} {start,end,offset}",[2,0,0,0,2,0,0,start,offset,0,end,offset,0,tolerance]
    for index in range(100):
        if index%2:
            origin=[random_source.uniform(-1e8,1e8) for _ in range(3)]
            ray=[random_source.uniform(-10,10) for _ in range(3)]
            points=[origin, [a+b for a,b in zip(origin,ray)], [a+0.25*b for a,b in zip(origin,ray)], [a+1.25*b for a,b in zip(origin,ray)]]
            values=[v for point in points for v in point]
        else: values=[random_source.uniform(-100,100) for _ in range(12)]
        yield f"random segments {index}",[2,*values,1e-6]
    for nonfinite in (math.nan,math.inf,-math.inf):
        for axis in range(12):
            points=[0,0,0,2,0,0,1,0,0,3,0,0]
            points[axis]=nonfinite
            yield f"nonfinite segment {axis} {nonfinite}",[2,*points,1e-7]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source",type=Path,required=True)
    parser.add_argument("--compiler",type=Path,default=ROOT/"build"/("dynlex.exe" if sys.platform=="win32" else "dynlex"))
    args=parser.parse_args()
    source=args.source.resolve()
    git=["git","-c",f"safe.directory={source.as_posix()}","-C",source]
    assert run([*git,"rev-parse","HEAD"]).strip()==PIN
    run([*git,"diff","--exit-code","HEAD","--","src/space/curve.rs","src/space/vec.rs"])
    directory=ROOT/"build/cadkernel-curve3-checks"
    directory.mkdir(parents=True,exist_ok=True)
    driver=directory/"reference-driver.rs"
    driver.write_text(
        '#![allow(dead_code)]\nmod space {\n'
        f'#[path=r"{(source/"src/space/vec.rs").as_posix()}"] pub mod vec;\n'
        f'#[path=r"{(source/"src/space/curve.rs").as_posix()}"] pub mod curve;\n'
        '}\n'
        f'include!(r"{(Path(__file__).parent/"reference.rs").as_posix()}");\n',encoding="utf-8")
    rustc=shutil.which("rustc") or Path.home()/".cargo/bin/rustc.exe"
    reference=directory/"reference.out"
    compile_program([rustc,"--edition=2021","--crate-name","curve3_reference",driver,"-O","-o",reference])
    upstream=directory/"upstream-tests.out"
    compile_program([rustc,"--edition=2021","--crate-name","curve3_tests","--test",driver,"-O","-o",upstream])
    result=run([upstream,"space::curve::tests::"])
    assert "12 passed" in result,result
    print("12 original Rust tests passed",flush=True)
    executables=[reference]
    for level in ("O0","O2"):
        for name in ("cadkernel_curve3","cadkernel_curve3_negative_segments","cadkernel_curve3_overflow_segments"):
            print(name,level,fixtures.verify_fixture(ROOT/"tests/required"/name,level,args.compiler.resolve(),directory,30,15,False),flush=True)
        probe=directory/f"probe-{level}.out"
        compile_program([args.compiler.resolve(),Path(__file__).parent/"probe.dl",f"-{level}","-o",probe])
        executables.append(probe)
    count=comparisons=0
    for label,values in cases():
        outputs=[[parse_number(v) for v in run([program,*values]).splitlines()] for program in executables]
        expected,unoptimized,optimized=outputs
        assert len(expected)==len(unoptimized)==len(optimized),(label,[len(v) for v in outputs])
        for field,wanted in enumerate(expected):
            discrete=(values[0]==2 or field==3)
            for actual in (unoptimized[field],optimized[field]):
                assert same(actual,wanted,exact=discrete),(label,field,wanted,actual,values)
                comparisons+=1
            assert same(unoptimized[field],optimized[field],exact=True),(label,"optimization mismatch",field)
        count+=1
        if count%100==0: print(f"{count} cases passed",flush=True)
    print(f"{count} cases, {comparisons} Rust comparisons passed; exact O0/O2 numerical parity",flush=True)
if __name__=="__main__": main()
