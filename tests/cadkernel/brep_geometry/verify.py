#!/usr/bin/env python3
"""Compare every B-rep geometry method with the pinned unmodified Rust module."""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path
import random
import re
import shutil
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[3]
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/"tests/cadkernel"))
import verify as fixtures
PIN="953d546b68aef4b6692566a1a9b077fc5bd9fb4f"

def run(command,timeout=45):
    status,output,_=fixtures.run_process([str(x) for x in command],timeout=timeout,cwd=ROOT)
    if status: raise AssertionError(f"exit {status}: {command}\n{output}")
    return output

def compile_program(command):
    output=run(command,90)
    if output.strip(): raise AssertionError(f"unexpected compiler diagnostics:\n{output}")

def parse(text):
    return [math.nan if re.fullmatch(r"[+-]?nan(?:\([a-zA-Z0-9_]+\))?",x,re.I) else float(x) for x in text.splitlines()]

def same(actual,expected,exact=False):
    if math.isnan(expected): return math.isnan(actual)
    if actual==expected:
        return actual!=0.0 or math.copysign(1.0,actual)==math.copysign(1.0,expected)
    if exact or actual==0.0 or expected==0.0: return False
    return math.isfinite(actual) and math.isfinite(expected) and math.isclose(actual,expected,rel_tol=3e-12,abs_tol=0.0)

def discrete_fields(mode,values):
    if mode==1:
        assert len(values)==8,len(values)
        return {7}
    result=set()
    index=3
    for size in (6,3,2):
        result.add(index)
        flag=values[index]
        assert flag in (0.0,1.0),values
        index+=1+int(flag)*size
    result.add(index)
    hits=values[index]
    assert hits in (0.0,1.0),values
    index+=1
    if hits:
        result.add(index)
        count=values[index]
        assert count in (0.0,1.0,2.0),values
        index+=1+int(count)
    index+=1
    result.update((index,index+1))
    assert values[index] in (0.0,1.0),values
    index+=1
    framed=values[index]
    assert framed in (0.0,1.0),values
    index+=1+int(framed)*9
    result.add(index)
    assert index+1==len(values),(index,len(values))
    return result

def cases():
    base=[0,0,0.,0.,0.,1.,0.,0.,0.,1.,0.,3.,1.,0.3,0.4,0.7,2.,3.,4.,0.2,-0.7,1.,1e-9]
    for mode,count in ((0,6),(1,5)):
        for kind in range(count):
            for parameter in (-2.,-0.,0.,0.1,0.8,1.,2.,math.pi,5.,1e15,math.nan,math.inf):
                value=base.copy()
                value[0:2]=mode,kind
                value[14]=parameter
                value[15]=parameter*0.7
                yield f"parameters {mode} {kind} {parameter}",value
            for field in (*range(2,14),*range(16,23)):
                for number in (-0.,0.,-1.,1e-200,1e154,1e308,math.nan,math.inf,-math.inf):
                    value=base.copy()
                    value[0:2]=mode,kind
                    value[field]=number
                    yield f"boundary {mode} {kind} {field} {number}",value
    # Degenerate directions force the linear and constant root paths.
    for kind in (0,1,2,3,4):
        for origin in ([0.,0.,0.],[3.,0.,0.],[0.,0.,3.],[math.inf,0.,0.]):
            for direction in ([0.,0.,0.],[1.,0.,0.],[-1.,0.,0.],[0.,0.,1.],[1e-154,0.,0.]):
                value=base.copy()
                value[1]=kind
                value[16:19]=origin
                value[19:22]=direction
                yield f"ray {kind} {origin} {direction}",value
    for major,minor in ((2.,3.),(3.,3.),(0.,2.),(-2.,3.)):
        for u in (0.,0.3,math.pi):
            for v in (0.,2.,math.pi,4.):
                value=base.copy()
                value[1]=4
                value[11:13]=major,minor
                value[14:16]=u,v
                ring=major+minor*math.cos(v)
                value[16:19]=ring*math.cos(u),ring*math.sin(u),minor*math.sin(v)
                yield f"torus inverse {major} {minor} {u} {v}",value
    rng=random.Random(7109546)
    for index in range(400):
        value=base.copy()
        mode=index%2
        value[0:2]=mode,rng.randrange(6 if mode==0 else 5)
        for field in range(2,22): value[field]=rng.uniform(-10.,10.)
        if index%3==0:
            value[2:5]=[rng.uniform(1e6,2e6) for _ in range(3)]
        yield f"random {index}",value

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source",required=True,type=Path)
    parser.add_argument("--compiler",type=Path,default=ROOT/"build"/("dynlex.exe" if sys.platform=="win32" else "dynlex"))
    parser.add_argument("--rust-toolchain",default="stable-x86_64-pc-windows-msvc" if sys.platform=="win32" else None)
    parser.add_argument("--limit",type=int,help="run only this many differential cases; records partial coverage")
    args=parser.parse_args()
    source=args.source.resolve()
    git=["git","-c",f"safe.directory={source.as_posix()}","-C",source]
    assert run([*git,"rev-parse","HEAD"]).strip()==PIN
    modules=["src/brep/geometry.rs","src/space/vec.rs","src/space/plane.rs","src/space/spline.rs","src/space/nurbs.rs","src/geom2d/vec.rs","src/geom2d/nurbs.rs","src/tessellation.rs"]
    run([*git,"diff","--exit-code","HEAD","--",*modules])
    output=Path(tempfile.mkdtemp(prefix="brep-geometry-",dir=ROOT/"build"))
    def module(name,path):
        return f'#[path=r"{(source/path).as_posix()}"] pub mod {name};\n'
    text='#![allow(dead_code)]\n'+module("tessellation","src/tessellation.rs")+"mod space {\n"
    for name in ("vec","plane","spline","nurbs"): text+=module(name,f"src/space/{name}.rs")
    text+="pub use vec::Vec3; pub use plane::Plane; pub use nurbs::{NurbsCurve3,NurbsSurface3};\n}\nmod geom2d {\n"
    text+=module("vec","src/geom2d/vec.rs")+module("nurbs","src/geom2d/nurbs.rs")
    text+="pub use nurbs::NurbsCurve;\n}\nmod brep {\n"+module("geometry","src/brep/geometry.rs")+"}\n"
    text+=f'include!(r"{(HERE/"reference.rs").as_posix()}");\n'
    driver=output/"reference-driver.rs"
    driver.write_text(text,encoding="utf-8")
    if args.rust_toolchain:
        rustup=shutil.which("rustup") or Path.home()/".cargo/bin/rustup.exe"
        rustc=[rustup,"run",args.rust_toolchain,"rustc"]
    else:
        rustc=[shutil.which("rustc") or Path.home()/".cargo/bin/rustc.exe"]
    version=run([*rustc,"--version","--verbose"])
    reference=output/"reference.out"
    compile_program([*rustc,"--edition=2021","--crate-name","geometry_reference",driver,"-O","-o",reference])
    upstream=output/"upstream-tests.out"
    compile_program([*rustc,"--edition=2021","--crate-name","geometry_tests","--test",driver,"-O","-o",upstream])
    original=run([upstream,"brep::geometry::tests::"])
    assert "12 passed" in original,original
    print("12 original Rust tests passed",flush=True)
    programs=[reference]
    for level in ("O0","O2"):
        for name in ("cadkernel_brep_geometry","cadkernel_brep_geometry_extra","cadkernel_brep_geometry_ownership"):
            print(name,level,fixtures.verify_fixture(ROOT/"tests/required"/name,level,args.compiler.resolve(),output,60,30,False),flush=True)
        binary=output/f"probe-{level}.out"
        compile_program([args.compiler.resolve(),HERE/"probe.dl",f"-{level}","-o",binary])
        programs.append(binary)
    checked=comparisons=0
    all_cases=list(cases())
    selected=all_cases if args.limit is None else all_cases[:args.limit]
    (output/"cases.json").write_text(json.dumps(selected,indent=2),encoding="utf-8")
    print(f"Running {len(selected)} of {len(all_cases)} cases; artifacts {output}",flush=True)
    for label,values in selected:
        results=[parse(run([program,*values])) for program in programs]
        expected,unoptimized,optimized=results
        assert len(expected)==len(unoptimized)==len(optimized),(label,[len(v) for v in results],values)
        discrete=discrete_fields(values[0],expected)
        for index,wanted in enumerate(expected):
            for actual in (unoptimized[index],optimized[index]):
                assert same(actual,wanted,exact=index in discrete),(label,index,wanted,actual,values,output)
                comparisons+=1
            assert same(unoptimized[index],optimized[index],exact=True),(label,"O0/O2",index,values,output)
        checked+=1
        if checked%100==0: print(f"{checked} cases passed",flush=True)
    report={"source":PIN,"rustc":version,"compiler_sha256":hashlib.sha256(args.compiler.read_bytes()).hexdigest(),
            "cases":checked,"total_cases":len(all_cases),"rust_comparisons":comparisons,"exact_optimization_parity":True,
            "complete":checked==len(all_cases)}
    (output/"report.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(report,indent=2),flush=True)

if __name__=="__main__": main()
