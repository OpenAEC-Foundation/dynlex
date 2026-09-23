# SPDX-License-Identifier: MPL-2.0
"""Pinned-source differential and independent arclength geometry checks."""
import argparse
import json
import math
import os
from pathlib import Path
import random
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
PIN = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"
sys.path.insert(0, str(ROOT / "scripts"))
from process_error_mode import unattended_child_processes


def run(command, timeout=60):
    with unattended_child_processes():
        result = subprocess.run([str(x) for x in command], cwd=ROOT, capture_output=True, text=True, timeout=timeout)
    if result.returncode:
        raise AssertionError(f"exit {result.returncode}: {command}\n{result.stdout}{result.stderr}")
    return result.stdout


def parsed(output):
    return [math.nan if re.fullmatch(r"[+-]?nan(?:\([\w]+\))?", x, re.I) else float(x) for x in output.split()]


def equivalent(a, b, exact=False):
    if math.isnan(b):
        return math.isnan(a)
    if a == b:
        return a != 0.0 or math.copysign(1., a) == math.copysign(1., b)
    return not exact and math.isfinite(a) and math.isfinite(b) and math.isclose(a, b, rel_tol=2e-12, abs_tol=2e-13)


def case(points, kind=0, degree=1, knots=(), weights=(), closed=False, parameter=.5, distance=5.):
    return [kind, degree, len(points), len(knots), len(weights), int(closed), parameter, distance,
            *(v for point in points for v in point), *knots, *weights]


def clamped(degree, count):
    return [0.] * (degree + 1) + [i / (count - degree) for i in range(1, count - degree)] + [1.] * (degree + 1)


def cases():
    poly = [[0.,0.,0.],[3.,0.,0.],[3.,4.,0.]]
    line = [[0.,0.,0.],[0.,0.,5.]]
    yield "original-polyline", case(poly), {0:1.,1:7.,2:0.,3:3.,4:0.,5:0.,6:0.,7:4.,8:0.,9:.75,10:3.,11:2.,12:0.,13:7.}
    yield "original-spline", case(line,1,knots=[0.,0.,1.,1.],weights=[1.,1.],parameter=.4,distance=2.), {0:1.,1:5.,2:0.,3:0.,4:0.,5:2.,6:0.,7:0.,8:5.,9:.4,12:2.}
    for label, points in (("empty",[]),("singleton",poly[:1]),("all-duplicate",[poly[0]]*4),
                          ("overflow",[[-1e308,0.,0.],[1e308,0.,0.]]),("underflow",[[0.,0.,0.],[1e-200,0.,0.]])):
        yield label, case(points), {0:0.}
    for closed in (False, True):
        for label, points in (("ordinary",poly),("adjacent-duplicates",[poly[0],poly[0],poly[1],poly[1],poly[2]]),
                              ("already-closed",poly+[poly[0]]),("reversed",list(reversed(poly))),
                              ("zero-length-station",[[0.,0.,0.],[1e-200,0.,0.],[1.,0.,0.]])):
            for parameter,distance in ((0.,0.),(.5,3.),(1.,7.),(-1e300,-math.inf),(1e300,math.inf),(math.nan,math.nan),(-0.,-0.)):
                yield f"poly-{label}-{closed}-{parameter}-{distance}", case(points,closed=closed,parameter=parameter,distance=distance), {}
    yield "closed-triangle", case(poly,closed=True), {0:1.,1:12.,2:1.}
    for bad in (math.nan,math.inf,-math.inf):
        for axis in range(3):
            points=[p.copy() for p in poly]
            points[1][axis]=bad
            yield f"nonfinite-point-{axis}-{bad}", case(points), {0:0.}
    for parameter in (math.nan,math.inf,-math.inf,-1e300,0.,.3,1.,1e300):
        yield f"spline-parameter-{parameter}", case(line,1,knots=[2.,2.,5.,5.],weights=[2.,2.],parameter=parameter,distance=math.nan), {0:1.,1:5.,9:0.}
    for weight in (1e-300,1e-15,1.,1e150,1e300):
        yield f"weight-normalization-{weight}", case(line,1,knots=[0.,0.,1.,1.],weights=[weight,weight],distance=2.), {0:1.,1:5.,12:2.}
    for bad in (0.,-1.,math.nan,math.inf,-math.inf):
        yield f"refused-weight-{bad}", case(line,2,knots=[0.,0.,1.,1.],weights=[bad,1.]), {0:0.}
    for bad in (math.nan,math.inf,-math.inf):
        yield f"spline-invalid-control-{bad}", case([[0.,0.,0.],[bad,0.,5.]],2,knots=[0.,0.,1.,1.],weights=[1.,1.]), {0:0.}
        yield f"spline-invalid-knot-{bad}", case(line,2,knots=[0.,0.,bad,1.],weights=[1.,1.]), {0:0.}
    for distance in (-math.inf, math.inf, -1e300, 1e300, -0.):
        yield f"spline-distance-{distance}", case(line,1,knots=[0.,0.,1.,1.],weights=[1.,1.],distance=distance), {0:1.,9:0. if distance<=0 else 1.,12:0. if distance<=0 else 5.}
    yield "normalized-weight-underflow", case(line,1,knots=[0.,0.,1.,1.],weights=[1e-300,1e300]), {0:0.}
    yield "overflowing-domain", case(line,1,knots=[-1e308,-1e308,1e308,1e308],weights=[1.,1.]), {0:0.}
    yield "zero-domain", case(line,2,knots=[0.,1.,1.,2.],weights=[1.,1.]), {0:0.}
    yield "constant-spline", case([line[0],line[0]],1,knots=[0.,0.,1.,1.],weights=[1.,1.]), {0:0.}
    yield "overflowing-chain", case([[0.,0.,0.],[1e308,0.,0.]],1,knots=[0.,0.,1.,1.],weights=[1.,1.]), {0:0.}
    for ratio in (1e-4,1e-6,1e-8,1e-12):
        yield f"sharp-rational-{ratio}", case(line,1,knots=[0.,0.,1.,1.],weights=[ratio,1.],distance=2.), {}
    quarter=[[1.,0.,0.],[1.,1.,0.],[0.,1.,0.]]
    for parameter in (0.,.25,.5,.75,1.):
        oracle={0:1.,1:math.pi/2,10:math.sqrt(.5),11:math.sqrt(.5),12:0.}
        if parameter==.5:
            oracle.update({3:math.sqrt(.5),4:math.sqrt(.5),5:0.})
        yield f"quarter-circle-{parameter}", case(quarter,1,2,clamped(2,3),[1.,math.sqrt(.5),1.],parameter=parameter,distance=math.pi/4), oracle
    for closed in (False,True):
        yield f"periodicity-{closed}", case(poly,1,2,clamped(2,3),[1.,2.,1.],closed=closed), {0:1.,2:float(closed)}
    for knot in (.25,.5,.75):
        yield f"piecewise-linear-{knot}", case([[0.,0.,0.],[1.,0.,0.],[1.,2.,0.],[4.,2.,0.]],1,1,[0.,0.,knot,knot,1.,1.],[1.]*4,parameter=knot,distance=1.), {}
    rng=random.Random(95354683)
    for index in range(36):
        degree=rng.randrange(1,5)
        count=degree+rng.randrange(1,5)
        points=[[rng.uniform(-8.,8.) for _ in range(3)] for _ in range(count)]
        yield f"random-poly-{index}", case(points,closed=index%2==0,parameter=rng.uniform(-.2,1.2),distance=rng.uniform(0.,40.)), {}
        yield f"random-spline-{index}", case(points,1,degree,clamped(degree,count),[rng.uniform(.2,4.) for _ in points],parameter=rng.random(),distance=rng.uniform(0.,30.)), {}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source",required=True,type=Path)
    parser.add_argument("--compiler",type=Path,default=ROOT/"build"/("dynlex.exe" if os.name=="nt" else "dynlex"))
    parser.add_argument("--reference-only",action="store_true")
    parser.add_argument("--filter",default="")
    args=parser.parse_args()
    source=args.source.resolve()
    git=["git","-c",f"safe.directory={source.as_posix()}","-C",source]
    if run([*git,"rev-parse","HEAD"]).strip()!=PIN:
        raise AssertionError("wrong source revision")
    modules=["space/vec.rs","space/polygon.rs","space/spline.rs","space/nurbs.rs","space/arclength.rs","tessellation.rs"]
    run([*git,"diff","--exit-code","HEAD","--",*("src/"+p for p in modules)])
    if (source/"src/space/arclength.rs").read_text().count("#[test]")!=1:
        raise AssertionError("unexpected source test count")
    directory=ROOT/"build/cadkernel-arclength3-checks"
    directory.mkdir(parents=True,exist_ok=True)
    suffix=".exe" if os.name=="nt" else ".out"
    def module(name,path):
        return f'#[path=r"{path.as_posix()}"] pub mod {name};\n'
    text='#![allow(dead_code)]\n'+module("tessellation",source/"src/tessellation.rs")+'mod space {\n'
    for name in ("vec","polygon","spline","nurbs","arclength"):
        text+=module(name,source/f"src/space/{name}.rs")
    text+='pub use vec::Vec3; pub use nurbs::NurbsCurve3;\n}\n'
    text+=f'include!(r"{(HERE/"reference.rs").as_posix()}");\n'
    driver=directory/"reference-driver.rs"
    driver.write_text(text,encoding="utf-8")
    rustc=shutil.which("rustc") or Path.home()/".cargo/bin/rustc.exe"
    reference=directory/f"reference{suffix}"
    run([rustc,"--edition=2021","--crate-name","arclength3_reference",driver,"-O","-o",reference])
    upstream=directory/f"upstream{suffix}"
    run([rustc,"--edition=2021","--crate-name","arclength3_tests","--test",driver,"-O","-o",upstream])
    original=run([upstream,"space::arclength::tests::"])
    if "1 passed" not in original:
        raise AssertionError(original)
    print(original,flush=True)
    programs=[]
    if not args.reference_only:
        for level in ("O0","O2"):
            for name,fragment in (("wrong_dimension","cad vector2"),("local_visibility","This pattern couldn't be resolved")):
                with unattended_child_processes():
                    rejected=subprocess.run([str(args.compiler.resolve()),str(HERE/f"{name}.dl"),f"-{level}","-o",str(directory/f"{name}-{level}{suffix}")],cwd=ROOT,capture_output=True,text=True,timeout=60)
                if rejected.returncode!=1 or fragment not in rejected.stdout+rejected.stderr:
                    raise AssertionError((name,level,rejected.returncode,rejected.stdout,rejected.stderr))
                print(f"{name} {level}: expected rejection",flush=True)
            for name in ("probe","ownership"):
                binary=directory/f"{name}-{level}{suffix}"
                started=time.monotonic()
                run([args.compiler.resolve(),HERE/f"{name}.dl",f"-{level}","-o",binary],timeout=120)
                print(f"{name} {level} compiled {time.monotonic()-started:.3f}s",flush=True)
                if name=="probe":
                    programs.append(binary)
                elif run([binary])!="arclength ownership passed\n":
                    raise AssertionError(f"ownership output mismatch: {level}")
    reports=[]
    fields=independent=0
    for label,data,oracle in cases():
        if args.filter not in label:
            continue
        values=[str(v) for v in data]
        expected=parsed(run([reference,*values],timeout=30))
        if len(expected)!=(17 if expected[0] else 1):
            raise AssertionError((label,"reference field count",expected))
        for index,wanted in oracle.items():
            if not equivalent(expected[index],wanted):
                raise AssertionError((label,"independent expectation",index,wanted,expected[index]))
            independent+=1
        observed=[]
        for program in programs:
            actual=parsed(run([program,*values],timeout=30))
            if len(actual)!=len(expected):
                raise AssertionError((label,program,"field count",expected,actual))
            for index,(wanted,got) in enumerate(zip(expected,actual)):
                if not equivalent(got,wanted,exact=data[0]==0 or index in (0,2)):
                    raise AssertionError((label,program,index,"Rust",wanted,"DynLex",got,"input",data))
                fields+=1
            observed.append(actual)
        if observed and any(not equivalent(a,b,exact=True) for a,b in zip(*observed)):
            raise AssertionError((label,"O0/O2 mismatch",observed))
        reports.append({"case":label,"valid":bool(expected[0]),"fields":len(expected),"reference":expected})
        if len(reports)%25==0:
            print(f"{len(reports)} cases passed",flush=True)
    if not reports:
        raise AssertionError("filter selected no cases")
    report={"pin":PIN,"reference_only":args.reference_only,"cases":len(reports),"comparisons":fields,"independent_assertions":independent,"results":reports}
    (directory/("reference-results.json" if args.reference_only else "results.json")).write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print(f"{'REFERENCE ONLY' if args.reference_only else 'PASS'}: {len(reports)} cases, {fields} Rust comparisons, {independent} independent assertions",flush=True)


if __name__=="__main__":
    main()
