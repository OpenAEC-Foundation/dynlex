# SPDX-License-Identifier: MPL-2.0
"""Reproduce complete construct/arc-fit source tests and native O0/O2 comparisons."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import random
import re
import shutil
import sys
ROOT=Path(__file__).resolve().parents[3]
HERE=Path(__file__).resolve().parent
sys.dont_write_bytecode=True
sys.path.insert(0,str(ROOT/"tests/cadkernel"))
from verify import run_process, verify_fixture
PIN="953d546b68aef4b6692566a1a9b077fc5bd9fb4f"
NAMES=("construct","arc_fit","curve","polyline","vec","angle","nurbs","deviation","arclength")
MODULES=tuple(f"src/geom2d/{name}.rs" for name in NAMES)+("src/geom2d/mod.rs","src/tessellation.rs","src/space/spline.rs")

def run(command, data=b"", timeout=60):
    status,output,elapsed=run_process([str(x) for x in command],cwd=ROOT,timeout=timeout,standard_input=data)
    if status: raise AssertionError(f"exit {status}: {command}\n{output}")
    return output,elapsed

def construct(kind, first=(0.,0.),middle=(0.,1.),last=(2.,0.),a=1.,b=0.,c=1.):
    return [kind,*first,*middle,*last,a,b,c]
def chain(points=((0.,0.),(1.,1.),(2.,0.)),closed=False,directions=None):
    if directions is None:directions=[None]*len(points)
    return [7,len(points),int(closed),len(directions),*(x for p in points for x in p),
            *(x for p in directions for x in ([0,0.,0.] if p is None else [1,*p]))]

def cases():
    special=(-math.inf,-1e308,-1.,-1e-9,-1e-12,-5e-324,-0.,0.,5e-324,1e-300,1e-12,1e-9,1.,2.,math.pi,math.tau,1e150,1e308,math.inf,math.nan)
    for kind in range(7):
        for value in special:
            yield f"construct-{kind}-scalar-{value}",construct(kind,a=value,b=value if kind==6 else 0.)
        for axis in range(6):
            for value in (-math.inf,-1e308,-0.,5e-324,1e308,math.inf,math.nan):
                data=construct(kind);data[axis+1]=value
                yield f"construct-{kind}-coordinate-{axis}-{value}",data
    for edge in (1e-12,1e-9,math.tau-1e-9,2.,math.tau):
        for value in (math.nextafter(edge,-math.inf),edge,math.nextafter(edge,math.inf)):
            for sign in (-1.,1.):
                for kind in range(7):
                    yield f"threshold-{kind}-{edge}-{value}-{sign}",construct(kind,last=(value,0.),a=sign*value,b=value)
    for a,b in ((-0.,0.),(0.,-0.),(1.,1.),(-1e308,1e308),(math.tau,0.),(1e16,-1e16),(1e-16,0.),(0.,1e-9)):
        yield f"bounded-angle-{a}-{b}",construct(0,b=a,c=b)
    for delta in (0.,1e-15,1e-13,1e-12,1e-9,1e-6):
        for sign in (-1.,1.):
            yield f"collinear-{delta}-{sign}",construct(5,middle=(1.,sign*delta))
            yield f"start-tangent-{delta}-{sign}",construct(4,middle=(1.,sign*delta))
    rng=random.Random(953546)
    for i in range(300):
        v=lambda:tuple(rng.uniform(-10.,10.) for _ in range(2))
        yield f"construct-random-{i}",construct(i%7,first=v(),middle=v(),last=v(),a=rng.uniform(-7,7),b=rng.uniform(-20,20),c=rng.uniform(-20,20))
    for points in ((),((0.,0.),),((0.,0.),(3.,4.)),((0.,0.),(1.,1.),(2.,0.)),((0.,0.),(1.,0.),(0.,0.)),((0.,0.),(0.,0.)),
                   ((-0.,-0.),(0.,0.)),((0.,0.),(1.,0.),(2.,0.)),((1.,0.),(0.,1.),(-1.,0.),(0.,-1.))):
        for closed in (False,True):
            yield f"chain-shape-{points}-{closed}",chain(points,closed)
            yield f"chain-directions-length-{points}-{closed}",chain(points,closed,[None]*(len(points)+1))
    for pointIndex in range(3):
        for axis in range(2):
            for value in special:
                points=[[0.,0.],[1.,1.],[2.,0.]];points[pointIndex][axis]=value
                yield f"chain-coordinate-{pointIndex}-{axis}-{value}",chain(points)
                directions=[None]*3;direction=[1.,0.];direction[axis]=value;directions[pointIndex]=direction
                yield f"chain-direction-{pointIndex}-{axis}-{value}",chain(directions=directions)
    # Exercise the two knee branches, equal radii, inflection and directed straights.
    angles=[i*math.pi/12 for i in range(-12,13)]
    vectors=[(math.cos(t),math.sin(t)) for t in angles]+[(1.,0.),(-1.,0.),(0.,1.),(0.,-1.)]
    for i,first in enumerate(vectors):
        for j,last in enumerate(vectors):
            yield f"direction-grid-{i}-{j}",chain(((0.,0.),(2.,0.)),directions=[first,last])
    for threshold in (1e-12,1e-9):
        for length in (math.nextafter(threshold,0.),threshold,math.nextafter(threshold,math.inf)):
            yield f"chain-chord-threshold-{length}",chain(((0.,0.),(length,0.)))
            yield f"chain-direction-threshold-{length}",chain(directions=[(length,0.),None,None])
    for magnitude in (1e-200,1e-13,1e-12,1e-9,1e-6,1.,1e6,1e150,1e200,1e308):
        for closed in (False,True):
            points=[(0.,0.),(magnitude,magnitude),(2*magnitude,0.)]
            yield f"chain-scale-{magnitude}-{closed}",chain(points,closed)
    yield "partial-chain-refusal",chain([(0.,0.),(1.,0.),(1.0000000001,1e-10)],directions=[(1.,0.),(1.,0.),(0.,1.)])
    ignored=chain();ignored[-2:]=[math.nan,math.inf]
    yield "unspecified-nonfinite-payload-ignored",ignored
    for edge in (1e-12,1e-9):
        for value in (math.nextafter(edge,0.),edge,math.nextafter(edge,math.inf)):
            for sign in (-1.,1.):
                for end in ((1.,-sign*value),(0.,1.),(-1.,0.)):
                    yield f"tangent-cross-threshold-{value}-{sign}-{end}",chain([(0.,0.),(2.,0.)],directions=[(1.,sign*value),end])
    for delta in (-1e-10,-1e-12,-1e-14,0.,1e-14,1e-12,1e-10):
        yield f"equal-radius-join-{delta}",chain([(0.,0.),(2.,0.)],directions=[(0.,1.),(delta,-1.)])
    for i in range(500):
        n=rng.randrange(2,9)
        points=[(rng.uniform(-10.,10.),rng.uniform(-10.,10.)) for _ in range(n)]
        directions=[None if rng.random()<.6 else (rng.uniform(-1.,1.),rng.uniform(-1.,1.)) for _ in points]
        yield f"chain-random-{i}",chain(points,bool(i%2),directions)

def parse(output):
    records=[]
    for line in output.splitlines():
        if line.startswith("case "):
            if int(line[5:])!=len(records):raise AssertionError("record order")
            records.append([])
        else:
            records[-1].append(math.nan if re.fullmatch(r"[+-]?nan(?:\([\w]+\))?",line,re.I) else float(line))
    return records

def equal(a,b,exact=False):
    if math.isnan(a) or math.isnan(b):return math.isnan(a) and math.isnan(b)
    if a==b:return a!=0 or math.copysign(1.,a)==math.copysign(1.,b)
    return not exact and math.isfinite(a) and math.isfinite(b) and math.isclose(a,b,abs_tol=3e-12,rel_tol=3e-12)

def compare(actual,expected,names,label,exact=False):
    if len(actual)!=len(expected):raise AssertionError(f"{label}: record count")
    total=0
    for name,got,want in zip(names,actual,expected):
        if len(got)!=len(want):raise AssertionError(f"{label} {name}: {len(got)} fields != {len(want)}; {got[:2]} != {want[:2]}")
        for index,(a,b) in enumerate(zip(got,want)):
            if not equal(a,b,exact):raise AssertionError(f"{label} {name} field {index}: {a!r} != {b!r}")
        total+=len(got)
    return total

def invariants(records,data):
    checked=0
    for record,(_,case) in zip(records,data):
        if case[0]!=7 or record[0]!=1:continue
        n=int(case[1]);closed=case[2]!=0;points=case[4:4+2*n]
        vertices=[record[i:i+6] for i in range(2,len(record),6)]
        assert len(vertices)==int(record[1])
        originals=[v for v in vertices if v[5]==0.]
        assert len(originals)==n
        for i,v in enumerate(originals):
            assert v[:2]==points[2*i:2*i+2] and v[3]==i and v[4]==0.
        assert not closed or len(vertices)<=2*n
        for v in vertices:
            assert all(math.isfinite(x) for x in v)
            if v[5]:assert 0.<v[4]<1. and 0<=v[3]<(n if closed else n-1)
        if not closed:assert vertices[-1][2]==0.
        checked+=1
    return checked

def type_checks(compiler,output,level):
    cases=(("arc_fit2","wrong_point","a cad vector3"),
           ("arc_fit2","wrong_direction","a cad normalization2"),
           ("arc_fit2","wrong_direction_value","a cad vector3"),
           ("construct2","wrong_point","a cad vector3"),
           ("construct2","wrong_precision","a 32-bit floating-point number"),
           ("construct2","local_visibility","This pattern couldn't be resolved"),
           ("arc_fit2","local_visibility","This pattern couldn't be resolved"))
    for directory,name,expected in cases:
        target=output/f"{directory}-{name}-{level}.out"
        status,log,_=run_process([str(compiler),str(ROOT/f"tests/cadkernel/{directory}/{name}.dl"),f"-{level}","-o",str(target)],cwd=ROOT,timeout=60)
        (output/f"{directory}-{name}-{level}.log").write_text(log)
        assert status==1 and "Error:" in log and expected in log and "Warning:" not in log,(name,status,log)
    return len(cases)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source",type=Path,required=True)
    parser.add_argument("--compiler",type=Path,default=ROOT/("build/dynlex.exe" if os.name=="nt" else "build/dynlex"))
    parser.add_argument("--reference-only",action="store_true")
    parser.add_argument("--cases-only",action="store_true")
    args=parser.parse_args()
    source=args.source.resolve();compiler=args.compiler.resolve()
    output=ROOT/"build/cadkernel-construct2-checks";output.mkdir(parents=True,exist_ok=True)
    git=["git","-c",f"safe.directory={source.as_posix()}","-C",source]
    revision,_=run([*git,"rev-parse","HEAD"]);assert revision.strip()==PIN
    run([*git,"diff","--exit-code","HEAD","--",*MODULES])
    sourceTests=re.findall(r"#\[test\]\s*fn (\w+)",(source/"src/geom2d/arc_fit.rs").read_text())
    assert sourceTests==(ROOT/"tests/required/cadkernel_arc_fit2/expected.txt").read_text().strip().splitlines()
    assert len(sourceTests)==3
    # Ellipse is the exact struct/impl slice required by unchanged Curve; the
    # two modules under test and every other dependency are included whole.
    mod=(source/"src/geom2d/mod.rs").read_text(encoding="utf-8")
    start=mod.rfind("#[derive",0,mod.index("pub struct Ellipse"))
    end=mod.index("{",mod.index("impl Ellipse"))+1;depth=1
    while depth:depth+=(mod[end]=="{")-(mod[end]=="}");end+=1
    module=lambda n,p:f'#[path=r"{p.as_posix()}"] pub mod {n};\n'
    driver=module("tessellation",source/"src/tessellation.rs")+"mod space {\n"+module("spline",source/"src/space/spline.rs")+"}\nmod geom2d {\n"
    driver+="".join(module(n,source/f"src/geom2d/{n}.rs") for n in NAMES)
    driver+="pub use vec::Vec2;pub use curve::*;pub use nurbs::NurbsCurve;pub use construct::*;\n"+mod[start:end]+"\n}\n"
    driver+=f'include!(r"{(HERE/"reference.rs").as_posix()}");\n'
    driverPath=output/"reference-driver.rs";driverPath.write_text(driver,encoding="utf-8")
    rustc=[shutil.which("rustc")]+(["+stable-x86_64-pc-windows-msvc"] if os.name=="nt" else [])
    rustVersion,_=run([*rustc,"-vV"])
    reference=output/"reference.out";upstream=output/"upstream-tests.out"
    for target,flags in ((reference,[]),(upstream,["--test"])):
        log,elapsed=run([*rustc,"--edition=2021","--crate-name","construct2_reference","-A","dead_code",driverPath,"-O",*flags,"-o",target])
        (output/(target.stem+"-compile.txt")).write_text(log)
        print(f"compile {target.name}: {elapsed:.3f}s",flush=True)
    log,_=run([upstream,"geom2d::arc_fit::tests::"])
    (output/"upstream-tests.txt").write_text(log)
    assert "3 passed; 0 failed" in log
    print(log,flush=True)
    data=list(cases());names=[name for name,_ in data]
    payload=(str(len(data))+"\n"+"\n".join(" ".join(map(str,c)) for _,c in data)+"\n").encode()
    (output/"cases.txt").write_bytes(payload)
    (output/"case-names.json").write_text(json.dumps(names,indent=2))
    rustOutput,_=run([reference],payload);(output/"reference.txt").write_text(rustOutput)
    expected=parse(rustOutput)
    record={"revision":PIN,"cases":len(data),"rust":rustVersion,"source_sha256":{p:hashlib.sha256((source/p).read_bytes()).hexdigest() for p in MODULES},
            "original_tests":3,"valid_chain_invariants":invariants(expected,data)}
    if not args.reference_only:
        digest=hashlib.sha256(compiler.read_bytes()).hexdigest()
        actual={}
        fixtureChecks=[]
        rejectedTypes={}
        for level in ("O0","O2"):
            if not args.cases_only:
                for name in ("cadkernel_construct2","cadkernel_arc_fit2","cadkernel_arc_fit2_ownership","cadkernel_arc_fit2_invariants"):
                    status=verify_fixture(ROOT/"tests/required"/name,level,compiler,output,60,60,False)
                    print(f"{name}/{level}: {status}",flush=True)
                    fixtureChecks.append(f"{name}/{level}")
                rejectedTypes[level]=type_checks(compiler,output,level)
            target=output/f"probe-{level}.out"
            log,elapsed=run([compiler,HERE/"probe.dl",f"-{level}","-o",target]);assert not log.strip(),log
            print(f"compile probe-{level}: {elapsed:.3f}s",flush=True)
            log,_=run([target],payload)
            (output/f"{level}.txt").write_text(log);actual[level]=parse(log)
            record[level]=compare(actual[level],expected,names,level)
            assert invariants(actual[level],data)==record["valid_chain_invariants"]
            print(f"{level}: {len(data)} cases, {record[level]} Rust fields",flush=True)
        record["exact_O0_O2"]=compare(actual["O0"],actual["O2"],names,"O0/O2",True)
        assert digest==hashlib.sha256(compiler.read_bytes()).hexdigest(),"compiler changed during verification"
        record["compiler_sha256"]=digest
        record["required_fixtures"]=fixtureChecks
        record["rejected_types_and_private_helpers"]=rejectedTypes
        record["native_sha256"]={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in ("lib/cadkernel/construct2.dl","lib/cadkernel/arc_fit2.dl","lib/cadkernel/curve2.dl","lib/cadkernel/polyline2.dl")}
    (output/"report.json").write_text(json.dumps(record,indent=2))
    print(json.dumps({k:v for k,v in record.items() if k not in ("rust","source_sha256","native_sha256")},indent=2),flush=True)
if __name__=="__main__":main()
