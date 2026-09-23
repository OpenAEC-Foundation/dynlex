# SPDX-License-Identifier: MPL-2.0
"""Native planar NURBS parity against unchanged pinned Rust modules."""
import argparse
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
sys.path.insert(0, str(ROOT / "scripts"))
from process_error_mode import unattended_child_processes

PIN = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"


def run(command, timeout=60, check=True):
    started = time.perf_counter()
    with unattended_child_processes():
        result = subprocess.run([str(x) for x in command], cwd=ROOT,
                                capture_output=True, text=True, timeout=timeout)
    if check and result.returncode:
        raise AssertionError(f"exit {result.returncode}: {command}\n{result.stdout}\n{result.stderr}")
    return result, time.perf_counter() - started


def build(command):
    result, elapsed = run(command)
    if Path(command[0]).name.lower() in ("dynlex", "dynlex.exe"):
        if result.stdout.strip() or result.stderr.strip():
            raise AssertionError(f"unexpected compiler diagnostics:\n{result.stdout}{result.stderr}")
    print(f"compile {Path(command[-1]).name}: {elapsed:.3f}s", flush=True)


def clamped(degree, count):
    n = count - degree
    return [0.] * (degree + 1) + [i / n for i in range(1,n)] + [1.] * (degree + 1)


def case(points, degree=2, *, ctor=0, knots=(), weights=(), transform=0,
         amount=1., last=0.8, u=0.37, t=0.63, pieces=3, spacing=0,
         first=None, end=None, flags=3, target=(1.1,0.2)):
    return [ctor,degree,len(points),len(knots),len(weights),transform,amount,last,
            u,t,pieces,spacing,int(first is not None),int(end is not None),flags,
            *(x for p in points for x in p),*knots,*weights,
            *(first if first is not None else (0.,0.)),
            *(end if end is not None else (0.,0.)),*target]


def cases():
    p = [[0.,0.],[1.,2.],[3.,-1.],[4.,1.]]
    k = [0.,0.,0.,0.4,1.,1.,1.]
    w = [1.,3.,0.75,2.]
    for ctor in (0,1):
        for weights in (w,[1.]*4):
            for transform in range(6):
                for parameter in (-1.,0.,0.4,1.,2.):
                    amount = 1 if transform == 2 else 0.37
                    yield f"basic-{ctor}-{weights[1]}-{transform}-{parameter}", case(
                        p,ctor=ctor,knots=k,weights=weights,transform=transform,
                        amount=amount,u=parameter,t=parameter)
    for degree in (1,2,3,15,16,17,24):
        points = [[float(i),math.sin(i)] for i in range(degree+3)]
        for transform in (0,1,2,3,4,5):
            yield f"degree-{degree}-transform-{transform}", case(
                points,degree,ctor=1,knots=clamped(degree,len(points)),
                weights=[1.+(i%3)*0.4 for i in range(len(points))],
                transform=transform,amount=1 if transform==2 else 0.37,pieces=1)
    for amount in (0,1,2,4):
        yield f"elevation-{amount}",case(p,knots=k,weights=w,transform=2,amount=amount)
    for parameter in (-math.inf,-1.,0.,1e-13,0.2,0.4,0.9999999999999,1.,2.,math.inf,math.nan):
        for transform in (3,4):
            yield f"insert-split-{transform}-{parameter}",case(
                p,knots=k,weights=w,transform=transform,amount=parameter)
    for first,last in ((0.,1.),(1.,0.),(0.2,0.8),(0.8,0.2),(0.,0.4),
                       (0.4,1.),(0.4,0.4),(0.4,0.4000000000001),
                       (-2.,2.),(math.nan,0.8),(0.2,math.inf)):
        yield f"trim-{first}-{last}",case(p,knots=k,weights=w,transform=5,amount=first,last=last)
    for points,degree in (([],2),([[0.,0.]],1),(p,0),(p,4),(p,5)):
        for ctor in (0,1):
            yield f"invalid-{len(points)}-{degree}-{ctor}",case(points,degree,ctor=ctor,knots=k,weights=w)
    for weights in ([],[1.],[1.,0.,1.,1.],[1.,-1.,1.,1.],
                    [1.,math.nan,1.,1.],[1.,math.inf,1.,1.],
                    [1e-300]*4,[0.5e-15]*4,[1e-15]*4,[1e300]*4):
        for ctor in (0,1):
            for transform in (0,3):
                yield f"weights-{weights}-{ctor}-{transform}",case(
                    p,ctor=ctor,knots=k,weights=weights,transform=transform,amount=0.37)
    for knots in ([],[0.,1.],[0.]*7,[0.,0.,0.,0.,0.,1.,1.],
                  [0.,0.,0.,0.9,0.4,1.,1.],[0.,0.,0.,math.nan,1.,1.,1.],
                  [0.,0.,0.,0.4,1.,1.,math.inf],
                  [0.,0.,0.,5e-16,1.,1.,1.],[-4.,-3.,-2.,0.4,2.,3.,4.]):
        for ctor in (0,1):
            for transform in (0,1):
                safe = all(math.isfinite(x) for x in knots) and all(a<=b for a,b in zip(knots,knots[1:]))
                yield f"knots-{knots}-{ctor}-{transform}",case(
                    p,ctor=ctor,knots=knots,weights=w,transform=transform,
                    flags=3 if safe else 0,u=2.5e-16)
    for parameter in (math.nan,math.inf,-math.inf,-0.):
        yield f"parameter-{parameter}",case(p,knots=k,weights=w,u=parameter,t=parameter,target=(parameter,parameter))
    for pieces in (0,1,8,32):
        yield f"tessellation-{pieces}",case(p,knots=k,weights=w,pieces=pieces)
    for spacing in range(3):
        for ctor in (2,3):
            for endpoints in range(4):
                yield f"fit-{ctor}-{spacing}-{endpoints}",case(
                    p,ctor=ctor,spacing=spacing,
                    first=(1.,2.) if endpoints&1 else None,
                    end=(1.,-2.) if endpoints&2 else None)
            for count in (0,1,2,3):
                yield f"fit-count-{ctor}-{spacing}-{count}",case(p[:count],ctor=ctor,spacing=spacing)
            for tangent in ((0.,0.),(1e-10,0.),(1e-9,0.),(math.nan,0.),(math.inf,0.)):
                yield f"tangent-{ctor}-{spacing}-{tangent}",case(
                    p,ctor=ctor,spacing=spacing,first=tangent,end=tangent,flags=1)
            for points in ([[1.,1.]]*4,[[0.,0.],[1e-100,0.],[2e-100,0.]],
                           [[0.,0.],[1e200,0.],[2e200,0.]],
                           [[0.,0.],[math.nan,1.],[2.,0.]]):
                yield f"fit-extreme-{ctor}-{spacing}-{points}",case(
                    points,ctor=ctor,spacing=spacing,flags=0)
    polylines = [
        [],[[0.,0.]],[[1.,1.]]*4,
        [[0.,0.],[1.,0.]],[[0.,0.],[1.,0.],[2.,0.]],
        [[0.,0.],[1.,2.],[3.,1.]],
        [[0.,0.],[1.,0.8],[2.,-0.6],[3.,0.]],
        [[0.,0.],[2.,0.],[1.,0.],[3.,0.]],
        [[0.,0.],[3.,0.],[2.,0.],[1.,0.]],
        [[0.,0.],[1.,0.],[0.,0.]],
        [[0.,0.],[0.,0.],[1.,0.],[1.,0.],[2.,2.]],
        [[0.,0.],[1e-200,0.],[0.,1e-200]],
        [[0.,0.],[1e300,0.],[0.,1e300]],
        [[0.,0.],[math.nan,1.],[2.,0.]],
    ]
    for index,points in enumerate(polylines):
        for tolerance in (0.,1e-20,0.1,0.4,10.,-1.,math.nan,math.inf):
            yield f"polyline-{index}-{tolerance}",case(points,ctor=4,amount=tolerance,flags=3)
    rng=random.Random(953546)
    for index in range(60):
        degree=rng.randint(1,5);count=degree+rng.randint(1,6)
        points=[[rng.uniform(-10,10),rng.uniform(-10,10)] for _ in range(count)]
        weights=[rng.uniform(0.1,5.) for _ in range(count)]
        transform=index%6
        yield f"random-{index}",case(points,degree,ctor=1,knots=clamped(degree,count),
            weights=weights,transform=transform,amount=1 if transform==2 else 0.37,
            u=rng.uniform(-0.2,1.2),t=rng.random())


def parsed(text):
    return [math.nan if re.fullmatch(r"[+-]?nan(?:\([a-zA-Z0-9_]+\))?",s,re.I) else float(s)
            for s in text.splitlines()]


def equal(a,b,exact=False):
    if math.isnan(b):
        return math.isnan(a)
    if a == b:
        return a != 0 or math.copysign(1.,a)==math.copysign(1.,b)
    if math.isinf(a) or math.isinf(b):
        return False
    return not exact and math.isclose(a,b,rel_tol=3e-12,abs_tol=1e-300)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source",type=Path,required=True)
    parser.add_argument("--compiler",type=Path,default=ROOT/"build"/("dynlex.exe" if os.name=="nt" else "dynlex"))
    parser.add_argument("--reference-only",action="store_true")
    args=parser.parse_args()
    source=args.source.resolve();compiler=args.compiler.resolve()
    git=["git","-c",f"safe.directory={source.as_posix()}","-C",source]
    revision,_=run([*git,"rev-parse","HEAD"])
    if revision.stdout.strip()!=PIN:
        raise AssertionError("unexpected reference revision "+revision.stdout.strip())
    modules=["src/geom2d/nurbs.rs","src/geom2d/vec.rs","src/space/spline.rs"]
    run([*git,"diff","--exit-code","HEAD","--",*modules])
    if (source/modules[0]).read_text(encoding="utf-8").count("#[test]")!=35:
        raise AssertionError("expected 35 upstream tests")
    directory=ROOT/"build/cadkernel-nurbs2-checks";directory.mkdir(exist_ok=True)
    rustc=shutil.which("rustc")
    if rustc is None:
        raise RuntimeError("rustc is required")
    driver=directory/"reference-driver.rs"
    def module(name,path):
        return f'#[path=r"{path.as_posix()}"] pub mod {name};\n'
    text="mod space {\n"+module("spline",source/modules[2])+"}\nmod geom2d {\n"
    text+=module("vec",source/modules[1])+module("nurbs",source/modules[0])+"}\n"
    text+=f'include!(r"{(ROOT/"tests/cadkernel/nurbs2/reference.rs").as_posix()}");\n'
    driver.write_text(text,encoding="utf-8")
    reference=directory/"reference.out";upstream=directory/"upstream.out"
    build([rustc,"--edition=2021","--crate-name","nurbs2_reference",driver,"-O","-o",reference])
    build([rustc,"--edition=2021","--crate-name","nurbs2_tests","--test",driver,"-O","-o",upstream])
    result,_=run([upstream,"geom2d::nurbs::tests::"])
    if "35 passed" not in result.stdout:
        raise AssertionError(result.stdout)
    print(result.stdout,end="",flush=True)
    refusals={
        "wrong_bridge_dimension":("No overload matches call 'the cad nurbs2 vector","fixed array containing 3 items"),
        "wrong_bridge_precision":("No overload matches call 'the cad nurbs2 vector","32-bit floating-point"),
        "wrong_fit_dimension":("No overload matches call 'check cad nurbs2 tangent","fixed array containing 3 items"),
        "wrong_weights":("Variable 'ownedWeights' cannot change type","cad nurbs2 copied list of weights"),
        "local_visibility":("tests/cadkernel/nurbs2/local_visibility.dl:","This pattern couldn't be resolved"),
    }
    probes={}
    for level in (() if args.reference_only else ("O0","O2")):
        for name,fixture in (("main","cadkernel_nurbs2"),("ownership","cadkernel_nurbs2_ownership")):
            output=directory/f"{name}-{level}.out"
            build([compiler,f"tests/required/{fixture}/main.dl",f"-{level}","-o",output])
            result,_=run([output])
            if result.stdout!=(ROOT/f"tests/required/{fixture}/expected.txt").read_text(encoding="utf-8"):
                raise AssertionError(f"{name}/{level} output mismatch:\n{result.stdout}")
            print(f"{name}/{level} passed",flush=True)
        for name,fragments in refusals.items():
            result,_=run([compiler,f"tests/cadkernel/nurbs2/{name}.dl",f"-{level}",
                          "-o",directory/f"{name}-{level}.out"],check=False)
            output=result.stdout+result.stderr
            if result.returncode!=1 or any(s not in output for s in fragments):
                raise AssertionError(f"{name}/{level} wrong rejection ({result.returncode}):\n{output}")
            print(f"{name}/{level} expected rejection",flush=True)
        probes[level]=directory/f"probe-{level}.out"
        build([compiler,"tests/cadkernel/nurbs2/probe.dl",f"-{level}","-o",probes[level]])
    count=fields=0
    for name,data in cases():
        arguments=[str(x) for x in data]
        result,_=run([reference,*arguments],timeout=30)
        want=parsed(result.stdout)
        actuals=[]
        for level,probe in probes.items():
            result,_=run([probe,*arguments],timeout=30)
            actual=parsed(result.stdout)
            if len(actual)!=len(want):
                raise AssertionError(f"{name}/{level} field count {len(actual)} != {len(want)}; input={data}")
            for i,(a,b) in enumerate(zip(actual,want)):
                if not equal(a,b):
                    raise AssertionError(f"{name}/{level} field{i}: {a!r} != Rust {b!r}; input={data}")
            actuals.append(actual)
        if actuals:
            for i,(a,b) in enumerate(zip(*actuals)):
                if not equal(a,b,exact=True):
                    raise AssertionError(f"{name} field{i}: O0 {a!r} != O2 {b!r}")
        count+=1;fields+=len(want)*(len(probes) or 1)
        if count%100==0:
            print(f"{count} cases passed",flush=True)
    print(f"{'REFERENCE ONLY' if args.reference_only else 'PASS'}: {count} cases, {fields} numeric comparisons"
          + ("; DynLex not checked" if args.reference_only else "; exact O0/O2 parity"),flush=True)


if __name__=="__main__":
    with unattended_child_processes():
        main()
