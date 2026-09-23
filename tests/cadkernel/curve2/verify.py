# SPDX-License-Identifier: MPL-2.0
"""Native Curve2/Polyline2 verification against unchanged pinned Rust modules."""
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

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/"scripts"))
from process_error_mode import unattended_child_processes
PIN="953d546b68aef4b6692566a1a9b077fc5bd9fb4f"

def run(command,timeout=60,check=True):
    with unattended_child_processes():
        result=subprocess.run([str(x) for x in command],cwd=ROOT,capture_output=True,text=True,timeout=timeout)
    if check and result.returncode:
        raise AssertionError(f"exit {result.returncode}: {command}\n{result.stdout}\n{result.stderr}")
    return result

def build(command):
    start=time.perf_counter()
    result=run(command)
    if Path(command[0]).name.lower() in ("dynlex","dynlex.exe") and (result.stdout.strip() or result.stderr.strip()):
        raise AssertionError("unexpected diagnostics: "+result.stdout+result.stderr)
    print(f"compile {Path(command[-1]).name}: {time.perf_counter()-start:.3f}s",flush=True)

def clamped(degree,count):
    spans=count-degree
    return [0.]*(degree+1)+[i/spans for i in range(1,spans)]+[1.]*(degree+1)

def case(kind=0,*,t=.37,target=(.6,.2),density=8.,angle=.2,flags=3,
         vertices=((0.,0.,0.),(1.,2.,1.),(3.,-1.,-.5),(4.,1.,0.)),closed=False,
         degree=2,knots=(),weights=(),start=(0.,0.),end=(1.,1.),radius=2.,
         first=.1,last=.8,minor=1.,axis=(1.,0.)):
    return [kind,t,*target,density,angle,flags,len(vertices),int(closed),degree,len(knots),len(weights),
            *start,*end,radius,first,last,minor,*axis,*(x for v in vertices for x in v),*knots,*weights]

def cases():
    for start,end in (((1e16,0.),(1.,0.)),((math.inf,0.),(1.,0.)),((-0.,-0.),(0.,0.))):
        yield f"stored-line-endpoints-{start}-{end}",case(0,start=start,end=end)
    for kind in range(8):
        for t in (-math.inf,-1.,-1.000000001e-9,-1e-9,-0.,0.,.37,1.,1.+1e-9,1.+1.000001e-9,2.,math.inf,math.nan):
            yield f"kind-{kind}-parameter-{t}",case(kind,t=t,flags=3 if t==.37 else 2)
    for kind in (0,1,2,3,6,7):
        for radius in (-2.,-0.,0.,1e-200,1e200,math.inf,math.nan):
            yield f"radius-{kind}-{radius}",case(kind,radius=radius,flags=2)
        for value in (-0.,1e-13,1e-12,1e150,1e200,math.inf,math.nan):
            yield f"coordinates-{kind}-{value}",case(kind,start=(value,value),end=(value,0.),target=(value,0.),flags=2)
    for first,last in ((0.,0.),(1.,1.),(0.,2*math.pi),(1.,-.4),(14.,-14.),(-50.,70.),(math.nan,0.),(0.,math.inf)):
        for kind in (2,3):
            yield f"sweep-{kind}-{first}-{last}",case(kind,first=first,last=last,flags=2)
    for axis in ((0.,0.),(-1.,0.),(0.,1.),(.6,.8),(2.,3.)):
        yield f"ellipse-axis-{axis}",case(3,axis=axis)
    for kind in range(8):
        for angle in (-math.inf,-1.,-0.,0.,math.inf,math.nan,.05,math.pi):
            yield f"sampling-{kind}-{angle}",case(kind,angle=angle,density=angle)
    for kind in (0,5):
        for first,last in (((0.,0.),(0.,1.)),((1.,1.),(1.,0.)),((0.,0.),(1.,0.)),((1.,1.),(0.,1.)),
                           ((0.,.1),(0.,1.)),((0.,0.),(0.,1.+1e-13)),((1e-14,0.),(1e-14,1.))):
            vertices=[(*first,0.),(*last,0.)]
            yield f"rectangle-{kind}-{first}-{last}",case(kind,start=first,end=last,vertices=vertices,degree=1,flags=2)
    polylines=[[],[(2.,3.,0.)],[(0.,0.,1.)],
               [(0.,0.,0.),(0.,0.,0.)],[(0.,0.,1.),(0.,0.,0.)],
               [(0.,0.,0.),(1.,0.,0.),(1.,1.,0.),(0.,1.,0.)],
               [(0.,0.,1.),(2.,0.,-1.)],
               [(-1.,0.,-1.),(1.,0.,0.)],
               [(0.,0.,1e-12),(2.,0.,0.)],
               [(0.,0.,math.nextafter(1e-12,0.)),(2.,0.,0.)],
               [(0.,0.,-0.),(2.,0.,0.)],
               [(0.,0.,math.inf),(2.,0.,0.)],
               [(0.,0.,math.nan),(2.,0.,0.)],
               [(math.nan,0.,0.),(1.,0.,0.)],
               [(0.,0.,0.),(math.inf,0.,0.)]]
    for index,vertices in enumerate(polylines):
        for closed in (False,True):
            for first,last in ((0.,1.),(.25,.75),(.75,1.25),(1.,2.),(.5,.5),(-.1,.5),(.1,1.1000001),(math.nan,.5),(.1,math.inf)):
                finite=all(math.isfinite(x) for v in vertices for x in v)
                yield f"polyline-{index}-{closed}-{first}-{last}",case(4,vertices=vertices,closed=closed,
                    first=first,last=last,flags=3 if first==.25 and finite else 2)
    for b in (-math.inf,-1e300,-2.,-1.,-.41421356237309503,-1e-12,-0.,0.,math.nextafter(1e-12,0.),1e-12,.4,1.,2.,1e300,math.inf,math.nan):
        for chord in (0.,math.nextafter(1e-12,0.),1e-12,2.):
            yield f"bulge-{b}-chord-{chord}",case(8,end=(chord,0.),radius=b,flags=1 if math.isfinite(b) and abs(b)<100 and chord==2. else 0)
    for sign in (0.,-0.):
        for t in (0.,-0.,1.):
            yield f"bulge-zero-sign-{math.copysign(1,sign)}-{t}",case(8,start=(0.,sign),end=(2.,sign),radius=1.,t=t)
    for degree in (1,2,3,16,17,24):
        vertices=[(float(i),math.sin(i),0.) for i in range(degree+3)]
        yield f"nurbs-degree-{degree}",case(5,vertices=vertices,degree=degree,knots=clamped(degree,len(vertices)),weights=[1.+(i%3)*.3 for i in range(len(vertices))])
    for knots in ((0.,0.,0.,.5,1.,1.,1.),(0.,0.,0.,1e-12,1.,1.,1.),(0.,0.,0.,math.nextafter(1e-12,math.inf),1.,1.,1.),
                  (-2.,-2.,-2.,3.,6.,6.,6.),(0.,)*7):
        yield f"nurbs-knots-{knots}",case(5,knots=knots,flags=2 if len(set(knots))==1 else 3)
    # Degenerate direction forces the exact source depth limit (65,537 samples).
    yield "ellipse-depth-limit",case(3,radius=0.,minor=0.,flags=1)
    rng=random.Random(953546)
    for index in range(80):
        kind=index%8
        vertices=[(rng.uniform(-10,10),rng.uniform(-10,10),rng.uniform(-2,2)) for _ in range(rng.randint(3,7))]
        yield f"random-{index}",case(kind,t=rng.uniform(-.5,1.5),target=(rng.uniform(-4,4),rng.uniform(-4,4)),
            start=(rng.uniform(-2,2),rng.uniform(-2,2)),end=(rng.uniform(-2,2),rng.uniform(-2,2)),
            vertices=vertices,closed=bool(index%2),weights=[rng.uniform(.1,4.) for _ in vertices],
            radius=rng.uniform(.1,5.),minor=rng.uniform(.1,4.),first=rng.uniform(0.,.5),last=rng.uniform(.5,1.))

def parsed(text):
    return [math.nan if re.fullmatch(r"[+-]?nan(?:\([a-zA-Z0-9_]+\))?",s,re.I) else float(s) for s in text.splitlines()]
def reference_values(text):
    values=[];absolute=[]
    for line in text.splitlines():
        if line.startswith("e "):
            values.extend(float(x) for x in line[2:].split());absolute.extend([-1.,-1.])
        elif line.startswith("p "):
            geometry,*pair=[float(x) for x in line[2:].split()]
            scale=max([geometry,*[abs(x) for x in pair if math.isfinite(x)]])
            # Native libm implementations differ near trigonometric zeroes.
            # Bound coordinate error by 16 ULPs of the geometry's magnitude;
            # this scales down for tiny geometry and does not relax scalars.
            slack=16*math.ulp(scale) if math.isfinite(scale) else 0.
            values.extend(pair);absolute.extend([slack,slack])
        else:
            values.append(float(line));absolute.append(0.)
    return values,absolute
def equal(a,b,exact=False,absolute=0.):
    if math.isnan(b):return math.isnan(a)
    if a==b:return a!=0 or math.copysign(1.,a)==math.copysign(1.,b)
    if math.isinf(a) or math.isinf(b):return False
    return not exact and math.isclose(a,b,rel_tol=3e-12,abs_tol=absolute)

def rust_driver(source,directory):
    names=["curve","polyline","vec","angle","nurbs","deviation","arclength"]
    paths=["src/geom2d/"+n+".rs" for n in names]+["src/geom2d/mod.rs","src/tessellation.rs","src/space/spline.rs"]
    git=["git","-c",f"safe.directory={source.as_posix()}","-C",source]
    if run([*git,"rev-parse","HEAD"]).stdout.strip()!=PIN:raise AssertionError("incorrect source revision")
    run([*git,"diff","--exit-code","HEAD","--",*paths])
    for name in ("curve","polyline"):
        contents=(source/f"src/geom2d/{name}.rs").read_text(encoding="utf-8")
        expected=re.findall(r"#\[test\]\s*fn (\w+)",contents)
        actual=(ROOT/f"tests/required/cadkernel_{name}2/expected.txt").read_text(encoding="utf-8").splitlines()
        if len(expected)!=12 or expected!=actual:raise AssertionError(name+" source test mapping")
    # Copy the exact Ellipse struct+impl text; no reference geometry is rewritten.
    contents=(source/"src/geom2d/mod.rs").read_text(encoding="utf-8")
    start=contents.rfind("#[derive",0,contents.index("pub struct Ellipse"))
    opening=contents.index("{",contents.index("impl Ellipse"));depth=1;end=opening+1
    while depth:
        depth+=(contents[end]=="{")-(contents[end]=="}");end+=1
    ellipse=contents[start:end]
    def module(name,path):return f'#[path=r"{path.as_posix()}"] pub mod {name};\n'
    text=module("tessellation",source/"src/tessellation.rs")+"mod space {\n"+module("spline",source/"src/space/spline.rs")+"}\nmod geom2d {\n"
    text+="".join(module(name,source/f"src/geom2d/{name}.rs") for name in names)
    text+="pub use vec::Vec2; pub use curve::*; pub use nurbs::NurbsCurve;\n"+ellipse+"\n}\n"
    text+=f'include!(r"{(ROOT/"tests/cadkernel/curve2/reference.rs").as_posix()}");\n'
    driver=directory/"reference-driver.rs";driver.write_text(text,encoding="utf-8")
    return driver

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source",type=Path,required=True)
    p.add_argument("--compiler",type=Path,default=ROOT/"build"/("dynlex.exe" if os.name=="nt" else "dynlex"))
    p.add_argument("--case",default="")
    p.add_argument("--skip-build",action="store_true")
    args=p.parse_args()
    directory=ROOT/"build/cadkernel-curve2-checks";directory.mkdir(exist_ok=True)
    reference=directory/"reference.out";upstream=directory/"upstream.out"
    probes={level:directory/f"probe-{level}.out" for level in ("O0","O2")}
    if not args.skip_build:
        driver=rust_driver(args.source.resolve(),directory)
        rustc=shutil.which("rustc")
        if rustc is None:raise RuntimeError("rustc is required")
        build([rustc,"--edition=2021","--crate-name","curve2_reference",driver,"-O","-o",reference])
        build([rustc,"--edition=2021","--crate-name","curve2_tests","--test",driver,"-O","-o",upstream])
        for name in ("curve","polyline"):
            result=run([upstream,f"geom2d::{name}::tests::"])
            if "12 passed" not in result.stdout:raise AssertionError(result.stdout)
            print(result.stdout,end="",flush=True)
        for level,probe in probes.items():
            for name in ("curve2","polyline2"):
                binary=directory/f"{name}-main-{level}.out"
                build([args.compiler,f"tests/required/cadkernel_{name}/main.dl",f"-{level}","-o",binary])
                result=run([binary])
                if result.stdout!=(ROOT/f"tests/required/cadkernel_{name}/expected.txt").read_text(encoding="utf-8"):
                    raise AssertionError(name+" fixture output mismatch: "+result.stdout)
                print(name+"/"+level+" 12 source groups passed",flush=True)
            build([args.compiler,"tests/cadkernel/curve2/probe.dl",f"-{level}","-o",probe])
            ownership=directory/f"ownership-{level}.out"
            build([args.compiler,"tests/required/cadkernel_curve2_ownership/main.dl",f"-{level}","-o",ownership])
            if run([ownership]).stdout!=(ROOT/"tests/required/cadkernel_curve2_ownership/expected.txt").read_text(encoding="utf-8"):
                raise AssertionError("ownership output mismatch")
            print("ownership/"+level+" passed",flush=True)
            refusals={
                "wrong_point":("No overload matches call 'a cad line2 from", "cad vector3"),
                "wrong_bounds":("No overload matches call 'the cad rectangle side", "fixed array containing 3 items"),
                "wrong_precision":("No overload matches call 'the cad rectangle side", "32-bit floating-point number"),
                "wrong_vertices":("store at cannot store a cad vector2", "pointer to a cad polyline vertex2"),
                "wrong_payload":("No overload matches call 'the cad curve2 of", "cad vector2"),
                "local_visibility":("tests/cadkernel/curve2/local_visibility.dl:","This pattern couldn't be resolved"),
            }
            for name,fragments in refusals.items():
                result=run([args.compiler,f"tests/cadkernel/curve2/{name}.dl",f"-{level}","-o",directory/f"{name}-{level}.out"],check=False)
                if result.returncode!=1 or any(x not in result.stdout+result.stderr for x in fragments):
                    raise AssertionError(name+" wrong refusal: "+result.stdout+result.stderr)
                print(name+"/"+level+" expected rejection",flush=True)
    count=fields=0
    for name,data in cases():
        if args.case and args.case not in name:continue
        arguments=[str(x) for x in data]
        want,absolute=reference_values(run([reference,*arguments]).stdout)
        actuals=[]
        for level,probe in probes.items():
            actual=parsed(run([probe,*arguments]).stdout)
            if len(actual)!=len(want):raise AssertionError(f"{name}/{level} fields {len(actual)} != {len(want)}; input={data}")
            for i,(a,b) in enumerate(zip(actual,want)):
                if not equal(a,b,exact=absolute[i]<0,absolute=max(absolute[i],0.)):raise AssertionError(f"{name}/{level} field {i}: {a!r} != Rust {b!r}; input={data}")
            actuals.append(actual)
        for i,(a,b) in enumerate(zip(*actuals)):
            if not equal(a,b,True):raise AssertionError(f"{name} field {i}: O0 {a!r} != O2 {b!r}")
        count+=1;fields+=len(want)*2
        if count%50==0:print(f"{count} cases passed",flush=True)
    print(f"PASS: {count} cases, {fields} numeric Rust comparisons; exact O0/O2 parity",flush=True)

if __name__=="__main__":
    with unattended_child_processes():main()
