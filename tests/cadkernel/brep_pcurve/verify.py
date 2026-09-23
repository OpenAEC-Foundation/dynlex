# SPDX-License-Identifier: MPL-2.0
"""Complete pinned pcurve verification; all children use unattended run_process."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import random
import re
import runpy
import shutil
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
M = runpy.run_path(str(ROOT / "tests/cadkernel/arclength2/verify.py"))
COMMON, CURVE = M["COMMON"], M["CURVE"]
PIN = CURVE["PIN"]
XY = [0.,0.,0.,1.,0.,0.,0.,1.,0.]

def case(mode=0, sk=0, ck=1, radius=3., minor=2., angle=math.pi/4,
         cr=3., cm=1., tol=1e-9, first=0., last=math.tau,
         plane=XY, frame=XY, q=(0.,2.), variant=0):
    return [mode,sk,ck,radius,minor,angle,cr,cm,tol,first,last,*plane,*frame,*q,variant]

def inputs():
    for sk in range(6):
        for ck in range(5):
            for span in ((0.,1.),(0.,math.tau),(math.pi,0.),(0.,-math.tau),(5.,8.)):
                yield f"dispatch-{sk}-{ck}-{span}",case(sk=sk,ck=ck,first=span[0],last=span[1])
    for kind in (1,2):
        for first,last in ((0.,0.),(.2,.4),(.4,.2),(0.,1.5*math.pi),(1.5*math.pi,0.),
                           (2.,2.+math.tau),(2.,2.-math.tau),(0.,4.*math.tau),
                           (0.,math.tau-2e-9),(0.,math.tau-5e-10)):
            for sign in (1.,-1.):
                f=XY.copy();f[7]=sign
                yield f"planar-conic-{kind}-{first}-{last}-{sign}",case(ck=kind,frame=f,first=first,last=last)
    for sk in (1,2):
        for height in (-3.,0.,1.,2.999999):
            radius=3.-height if sk==2 else 3.
            f=XY.copy();f[2]=height
            yield f"band-{sk}-{height}",case(sk=sk,frame=f,cr=radius)
        for theta in (0.,math.pi/2,-math.pi/2,math.pi-1e-12,-math.pi+1e-12):
            c,s=math.cos(theta),math.sin(theta)
            f=[3.*c,3.*s,0.,0.,0.,1.,0.,1.,0.]
            if sk==2:f[3:6]=[-c,-s,1.]
            yield f"generator-{sk}-{theta}",case(sk=sk,ck=0,frame=f,first=-1.,last=1.)
    for sk in (3,4):
        for latitude in (-1.3,-.7,0.,.7,1.3):
            r=3.*math.cos(latitude) if sk==3 else 3.+2.*math.cos(latitude)
            z=3.*math.sin(latitude) if sk==3 else 2.*math.sin(latitude)
            f=XY.copy();f[2]=z
            yield f"latitude-{sk}-{latitude}",case(sk=sk,frame=f,cr=r)
        # Exact meridians, including both poles for sphere trims.
        f=[0.,0.,0.,1.,0.,0.,0.,0.,1.]
        if sk==4:f[0]=3.
        for first,last in ((-math.pi/2,math.pi/2),(math.pi/2,-math.pi/2),(0.,math.tau),(0.,-math.tau)):
            yield f"meridian-{sk}-{first}-{last}",case(sk=sk,frame=f,cr=3. if sk==3 else 2.,first=first,last=last)
    for first,last in ((0.,math.pi),(math.pi,0.),(0.,-math.pi),(0.,0.)):
        yield f"exact-sphere-pole-{first}-{last}",case(sk=3,frame=[0.,0.,0.,0.,0.,1.,1.,0.,0.],first=first,last=last)
    for major in (.5,2.,3.,-1.,0.):
        for first,last in ((0.,math.tau),(math.tau,0.),(-math.pi/2,math.pi/2)):
            yield f"torus-tube-{major}-{first}-{last}",case(sk=4,radius=major,minor=2.,frame=[major,0.,0.,1.,0.,0.,0.,0.,1.],cr=2.,first=first,last=last)
    # Non-axis sphere circles exercise the 96-sample pcurve and seam refusal.
    for theta in (.2,.7,1.1):
        for height in (0.,.3,1.5,2.9):
            st,ct=math.sin(theta),math.cos(theta)
            f=[height*st,0.,height*ct,ct,0.,-st,0.,1.,0.]
            yield f"sphere-general-{theta}-{height}",case(sk=3,frame=f,cr=math.sqrt(9.-height*height))
    values=(-math.inf,-1.,-0.,0.,5e-324,1e-12,1e150,math.inf,math.nan)
    for value in values:
        for index in (3,6,8,9,10,14,17,20,23,26):
            a=case();a[index]=value
            yield f"nonfinite-field-{index}-{value}",a
    for sk in range(4):
        for variant in range(12):
            yield f"body-{sk}-{variant}",case(mode=1,sk=sk,variant=variant,first=-math.pi,last=math.pi/2)
        for origin in ((0.,0.,0.),(512345.678,4512345.678,91.5),(-1e9,1e9,-1e9)):
            f=XY.copy();f[:3]=origin
            yield f"survey-body-{sk}-{origin}",case(mode=1,sk=sk,plane=f,tol=1e-6)
    for sk in range(6):
        for variant in range(7):
            for q in ((0.,2.),(1.,1.),(0.,4.1),(math.tau,0.),(-math.tau,0.),(math.tau,math.tau),(3.,0.)):
                yield f"containment-{sk}-{variant}-{q}",case(mode=2,sk=sk,ck=3,variant=variant,q=q,first=1.,last=math.tau)
    for flags in range(4):
        yield f"nurbs-periods-{flags}",case(mode=2,sk=5,ck=flags)
    for sk in range(6):
        for variant in range(12):
            yield f"stored-chain-{sk}-{variant}",case(mode=3,sk=sk,ck=3,variant=variant,first=1.,last=math.tau)
    for width in (0.,math.tau-2e-9,math.tau-5e-10,math.tau,2*math.tau):
        for level in (-0.,0.,1e-10,1e100):
            yield f"periodic-band-width-{width}-{level}",case(mode=2,sk=1,variant=5,first=level,last=width)
    rng=random.Random(68314)
    for i in range(80):
        f=XY.copy();f[:3]=[rng.uniform(-1e3,1e3),rng.uniform(-1e3,1e3),0.]
        first=rng.uniform(-7,7);last=first+rng.uniform(-7,7)
        yield f"random-conic-{i}",case(ck=1+i%2,frame=f,cr=rng.uniform(.01,20),cm=rng.uniform(.01,5),first=first,last=last)

WRAPPERS = """
pub fn verify_parts(b:&super::topology::Body,f:super::topology::FaceKey,t:f64)->Option<Vec<(super::topology::CoedgeKey,Curve)>>{face_boundary_parts(b,f,t)}
pub fn verify_periods(s:&Surface)->[Option<f64>;2]{periods(s)}
pub fn verify_contains(s:&Surface,b:&[Curve],p:[f64;2],t:crate::geom2d::Tolerance)->bool{contains_parameter(s,b,p,t)}
pub fn verify_band_point(s:&Surface,b:&[Curve],t:f64)->Option<[f64;3]>{periodic_band_point(s,b,t)}
"""

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source",type=Path,required=True)
    parser.add_argument("--compiler",type=Path,default=ROOT/"build/dynlex.exe")
    parser.add_argument("--rustc",type=Path,default=Path.home()/".rustup/toolchains/stable-x86_64-pc-windows-msvc/bin/rustc.exe")
    parser.add_argument("--dependencies",type=Path,default=ROOT/"build/topology-reference-deps/target/debug/deps")
    parser.add_argument("--output",type=Path,default=HERE/"_artifacts")
    parser.add_argument("--skip-build",action="store_true")
    parser.add_argument("--case",default="")
    args=parser.parse_args()
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    def run(cmd,check=True,timeout=90):
        code,text,elapsed=COMMON["run_process"]([str(x) for x in cmd],timeout=timeout,cwd=ROOT)
        if check:assert code==0,f"exit {code}: {cmd}\n{text}"
        return code,text,elapsed
    def compile(cmd,label):
        code,text,elapsed=run(cmd)
        (out/f"{label}.compile.log").write_text(text)
        if Path(cmd[0])==args.compiler:assert not text.strip(),text
        print(f"{label}: compile {elapsed:.3f}s",flush=True)
    source=args.source.resolve()
    _,revision,_=run(["git","-c",f"safe.directory={source.as_posix()}","-C",source,"rev-parse","HEAD"])
    assert revision.strip()==PIN
    run(["git","-c",f"safe.directory={source.as_posix()}","-C",source,"diff","--exit-code",PIN,"--","src","Cargo.toml"])
    names=re.findall(r"#\[test\]\s*fn (\w+)",(source/"src/brep/pcurve.rs").read_text())
    assert names==(HERE/"expected.txt").read_text().splitlines() and len(names)==14
    digest=hashlib.sha256(args.compiler.read_bytes()).hexdigest()
    _,rustversion,_=run([args.rustc,"-vV"])
    assert "x86_64-pc-windows-msvc" in rustversion
    deps=args.dependencies.resolve();externs=[]
    for name in ("spade","rustc_hash"):
        paths=list(deps.glob(f"lib{name}-*.rlib"));assert len(paths)==1
        externs += ["--extern",f"{name}={paths[0]}"]
    if not args.skip_build:
        copied=out/"rust-source"
        shutil.copytree(source/"src",copied/"src",dirs_exist_ok=True)
        module=copied/"src/brep/pcurve.rs"
        # Only visibility adapters are appended: every source byte remains intact.
        module.write_bytes((source/"src/brep/pcurve.rs").read_bytes()+WRAPPERS.encode())
        assert module.read_bytes().startswith((source/"src/brep/pcurve.rs").read_bytes())
        for mode in ("O0","O2"):
            opts=[args.rustc,"--edition=2021","--crate-name=cadkernel",copied/"src/lib.rs","--cfg",'feature="geom2d"',"--cfg",'feature="brep"',"-L",f"dependency={deps}",*externs,"-C",f"opt-level={mode[1]}"]
            library=out/f"libcadkernel-{mode}.rlib"
            compile([*opts,"--crate-type=rlib","-o",library],f"library-{mode}")
            test=out/f"upstream-{mode}.out"
            compile([*opts,"--test","-o",test],f"upstream-{mode}")
            _,text,_=run([test,"brep::pcurve::tests::","--test-threads=1"])
            assert "14 passed; 0 failed" in text,text
            (out/f"upstream-{mode}.txt").write_text(text)
            compile([args.rustc,"--edition=2021",HERE/"reference.rs","--extern",f"cadkernel={library}","-L",f"dependency={deps}","-C",f"opt-level={mode[1]}","-o",out/f"reference-{mode}.out"],f"reference-{mode}")
            for fixture in ("brep_pcurve","brep_pcurve_ownership","brep_pcurve_default_clone"):
                result=COMMON["verify_fixture"](ROOT/"tests/required"/("cadkernel_"+fixture),mode,args.compiler,out,90,30,False)
                print(f"{fixture}/{mode}: {result}",flush=True)
            compile([args.compiler,HERE/"probe.dl",f"-{mode}","-o",out/f"probe-{mode}.out"],f"probe-{mode}")
            for name in ("wrong_boundary","wrong_tolerance","wrong_key","wrong_point"):
                code,text,_=run([args.compiler,HERE/f"{name}.dl",f"-{mode}","-o",out/f"{name}-{mode}.out"],check=False)
                (out/f"{name}-{mode}.txt").write_text(text)
                assert code==1 and "No overload matches" in text,(name,code,text)
                print(f"{name}/{mode}: expected type rejection",flush=True)
    count=fields=0
    for name,values in inputs():
        if args.case not in name:continue
        argv=[str(x) for x in values]
        (out/"current-input.json").write_text(json.dumps(dict(name=name,values=argv)))
        actuals=[];references=[]
        for mode in ("O0","O2"):
            _,ref,_=run([out/f"reference-{mode}.out",*argv],timeout=30)
            want,limits=M["expected_values"](ref);references.append(want)
            _,text,_=run([out/f"probe-{mode}.out",*argv],timeout=30)
            (out/f"current-native-{mode}.txt").write_text(text)
            (out/f"current-rust-{mode}.txt").write_text(ref)
            got=CURVE["parsed"](text);actuals.append(got)
            assert len(got)==len(want),f"{name}/{mode}: fields {len(got)} != {len(want)}"
            for index,(a,b,limit) in enumerate(zip(got,want,limits)):
                assert CURVE["equal"](a,b,exact=limit<0,absolute=max(limit,0)),f"{name}/{mode} field {index}: {a} != Rust {b}"
            fields+=len(want)
        for label,outputs in (("native",actuals),("Rust",references)):
            assert len(outputs[0])==len(outputs[1]) and all(CURVE["equal"](a,b,exact=True) for a,b in zip(*outputs)),f"{name}: {label} optimization divergence"
        count+=1
        if count%25==0:print(f"{count} cases passed: {name}",flush=True)
    assert count and digest==hashlib.sha256(args.compiler.read_bytes()).hexdigest()
    paths=[ROOT/"lib/cadkernel/brep_pcurve.dl",*HERE.glob("*.dl"),HERE/"verify.py",HERE/"reference.rs",ROOT/"tests/cadkernel/brep_pcurve_ownership/main.dl"]
    ownership_groups = len((ROOT / "tests/required/cadkernel_brep_pcurve_ownership/expected.txt").read_text(encoding="utf-8").splitlines())
    report=dict(revision=PIN,compiler_sha256=digest,rustc=rustversion,source_tests=14,native_groups=14,ownership_groups=ownership_groups,default_clone_groups=1,type_rejections=8,cases=count,comparisons=fields,case_filter=args.case,exact_native_optimization_parity=True,exact_rust_optimization_parity=True)
    report["source_sha256"]={str(p.relative_to(source)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((source/"src").rglob("*.rs"))}
    report["native_sha256"]={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    report["reference_libraries"]={mode:hashlib.sha256((out/f"libcadkernel-{mode}.rlib").read_bytes()).hexdigest() for mode in ("O0","O2")}
    (out/("filtered-summary.json" if args.case else "summary.json")).write_text(json.dumps(report,indent=2)+"\n")
    print(f"PASS: {count} cases; {fields} Rust comparisons; artifacts={out}",flush=True)

if __name__=="__main__":main()
