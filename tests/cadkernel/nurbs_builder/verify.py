# SPDX-License-Identifier: MPL-2.0
"""Verify the complete rational B-rep builders against pinned Rust."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import runpy
import tempfile

ROOT=Path(__file__).resolve().parents[3]
CROSS=runpy.run_path(str(ROOT/"tests/cadkernel/cross2/verify.py"))
CURVE,MEASURE,COMMON=(CROSS[key] for key in ("CURVE","MEASURE","COMMON"))
run,compile_program=(CROSS[key] for key in ("run","compile_program"))


PLANAR=runpy.run_path(str(ROOT/"tests/cadkernel/planar3/verify.py"))
curve=PLANAR["curve"]
XY=PLANAR["XY"]


def case(name,mode=0,sweep=math.pi/2,offset=(1.,2.,3.),mutation=0,delta=0.,**kwargs):
    return name,[mode,sweep,*offset,mutation,delta,*curve(**kwargs)]


def inputs():
    for kind in range(8):
        for frame in (XY,PLANAR["UPRIGHT"],[5.,-2.,7.,2.,0.,0.,1.,3.,0.],[0.]*9):
            yield case(f"variant-{kind}-{frame}",kind=kind,frame=frame)
    for sweep in (-math.inf,-1.,-0.,0.,5e-324,1e-300,1e-12,1.,math.pi/2-1e-15,math.pi/2,math.pi/2+1e-15,math.pi,math.tau,10.,100.,math.inf,math.nan):
        yield case(f"unit-arc-{sweep}",mode=1,sweep=sweep)
    for kind in (1,2,3):
        for radius in (-2.,-0.,0.,1e-300,1e-12,2.,1e12,1e150,math.inf,math.nan):
            yield case(f"radius-{kind}-{radius}",kind=kind,radius=radius)
        for first,last in ((0.,0.),(5.,1.),(-8.,-2.),(0.,20.),(1e16,1e16+4.),(1e308,1e308),(-math.inf,1.),(math.nan,1.),(0.,math.inf)):
            yield case(f"angles-{kind}-{first}-{last}",kind=kind,first=first,last=last)
        for origin in ((-0.,-0.),(512345.678,4512345.678),(1e12,-1e12),(math.inf,0.),(math.nan,0.)):
            yield case(f"origin-{kind}-{origin}",kind=kind,start=origin)
    for axis in ((0.,0.),(0.,1.),(2.,3.),(-1.,-0.),(math.inf,1.),(math.nan,1.)):
        for minor in (-2.,0.,1e-200,5.):
            yield case(f"ellipse-axis-{axis}-{minor}",kind=3,axis=axis,minor=minor)
    for mutation in range(8):
        for delta in (-math.inf,-1.,-1.000001e-10,-1e-10,-.999999e-10,-0.,.999999e-10,1e-10,1.000001e-10,1.,math.inf,math.nan):
            yield case(f"compatibility-{mutation}-{delta}",kind=1,mutation=mutation,delta=delta)
    for knots in ((0.,0.,0.,1.,1.,1.),(2.,2.,2.,6.,6.,6.),(-1.,0.,0.,1.,1.,2.),(0.,0.,0.,0.,0.,0.),(0.,0.,2.,1.,1.,1.),(-1e308,-1e308,-1e308,1e308,1e308,1e308),(math.nan,0.,0.,1.,1.,1.)):
        for mode in (0,2):
            yield case(f"nurbs-knots-{mode}-{knots}",mode=mode,kind=5,knots=knots)
    for degree in (0,1,2,3,5):
        for points in ((),((0.,0.,0.),),((0.,0.,0.),(2.,1.,0.)),((0.,0.,0.),(2.,0.,0.),(0.,2.,0.))):
            for weights in ((),(1.,),(1.,1.,1.),(1.,0.,1.),(1.,-1.,1.),(1.,math.inf,1.),(1.,math.nan,1.)):
                yield case(f"raw-shape-{degree}-{points}-{weights}",mode=2,degree=degree,points=points,weights=weights)
    for value in (-0.,1e-150,1e150,math.inf,math.nan):
        for component in range(9):
            frame=XY.copy();frame[component]=value
            yield case(f"frame-{component}-{value}",kind=2,frame=frame)
        for component in range(3):
            offset=[1.,2.,3.];offset[component]=value
            yield case(f"translation-{component}-{value}",kind=2,offset=offset)
    import random
    rng=random.Random(972511)
    for index in range(60):
        yield case(f"random-conic-{index}",kind=rng.choice((1,2,3)),radius=rng.uniform(-20,20),minor=rng.uniform(-10,10),
            axis=(rng.uniform(-3,3),rng.uniform(-3,3)),start=(rng.uniform(-50,50),rng.uniform(-50,50)),
            first=rng.uniform(-7,7),last=rng.uniform(-7,7))


def rust_driver(source,output):
    space=("spline","vec","plane","nurbs")
    geom=("curve","polyline","vec","angle","nurbs","deviation","arclength")
    paths=["src/brep/nurbs_builder.rs","src/tessellation.rs","src/geom2d/mod.rs",*[f"src/space/{n}.rs" for n in space],*[f"src/geom2d/{n}.rs" for n in geom]]
    _,revision,_=run(["git","-c",f"safe.directory={source.as_posix()}","-C",source,"rev-parse","HEAD"])
    assert revision.strip()==CURVE["PIN"]
    run(["git","-c",f"safe.directory={source.as_posix()}","-C",source,"diff","--exit-code",CURVE["PIN"],"--",*paths])
    def module(name,path):return f'#[path=r"{(source/path).as_posix()}"] pub mod {name};\n'
    contents=(source/"src/geom2d/mod.rs").read_text(encoding="utf-8")
    start=contents.rfind("#[derive",0,contents.index("pub struct Ellipse"))
    end=contents.index("/// Distance below which",start)
    content=module("tessellation","src/tessellation.rs")+"mod space {\n"
    content+="".join(module(n,f"src/space/{n}.rs") for n in space)
    content+="pub use vec::Vec3;pub use plane::Plane;pub use nurbs::{NurbsCurve3,NurbsSurface3};\n}\nmod geom2d {\n"
    content+="".join(module(n,f"src/geom2d/{n}.rs") for n in geom)
    content+="pub use vec::Vec2;pub use curve::*;pub use nurbs::NurbsCurve;pub use polyline::{Polyline,PolylineVertex,BulgeArc};\n"+contents[start:end]+"\n}\n"
    content+="mod brep {\n"+module("nurbs_builder","src/brep/nurbs_builder.rs")+"}\n"
    content+=f'include!(r"{(ROOT/"tests/cadkernel/nurbs_builder/reference.rs").as_posix()}");\n'
    driver=output/"reference-driver.rs"
    driver.write_text(content,encoding="utf-8")
    return driver,paths

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source",type=Path,required=True)
    parser.add_argument("--compiler",type=Path,default=ROOT/"build/dynlex.exe")
    parser.add_argument("--rustc",type=Path,default=Path.home()/".rustup/toolchains/stable-x86_64-pc-windows-msvc/bin/rustc.exe")
    parser.add_argument("--case",default="")
    parser.add_argument("--output",type=Path)
    parser.add_argument("--skip-build",action="store_true")
    args=parser.parse_args()
    output=args.output or Path(tempfile.mkdtemp(prefix="nurbs_builder-",dir=ROOT/"build"))
    output.mkdir(parents=True,exist_ok=True)
    digest=hashlib.sha256(args.compiler.read_bytes()).hexdigest()
    _,version,_=run([args.rustc,"-vV"])
    if os.name=="nt":assert "host: x86_64-pc-windows-msvc" in version
    source=args.source.resolve()
    driver,paths=rust_driver(source,output)
    names=re.findall(r"#\[test\]\s*fn (\w+)",(source/"src/brep/nurbs_builder.rs").read_text())
    assert names==[], "source module has no unit tests"
    assert len((ROOT/"tests/cadkernel/nurbs_builder/expected.txt").read_text().splitlines())==8
    if not args.skip_build:
        for mode in ("O0","O2"):
            options=[args.rustc,"--edition=2021","--cfg",'feature="geom2d"',"--crate-name","nurbs_builder_reference",driver,"-C",f"opt-level={mode[1]}","-A","dead_code"]
            compile_program([*options,"-o",output/f"reference-{mode}.out"])
            for fixture in ("nurbs_builder","nurbs_builder_ownership"):
                print(COMMON["verify_fixture"](ROOT/"tests/cadkernel"/fixture,mode,args.compiler,output,60,30,False),flush=True)
            compile_program([args.compiler,ROOT/"tests/cadkernel/nurbs_builder/probe.dl",f"-{mode}","-o",output/f"probe-{mode}.out"])
            for name,fragment in (("wrong_points","32-bit integer"),("wrong_dimension","cad vector2"),("wrong_knots","32-bit floating-point number"),("wrong_weights","32-bit floating-point number")):
                binary=output/f"{name}-{mode}.out"
                status,text,_=COMMON["run_process"]([str(args.compiler),f"tests/cadkernel/nurbs_builder/{name}.dl",f"-{mode}","-o",str(binary)],timeout=60,cwd=ROOT,phase="compilation")
                (output/f"{name}-{mode}.txt").write_text(text)
                assert status==1 and fragment in text and not binary.exists(),(status,text)
                print(f"{name}/{mode}: expected typed refusal",flush=True)
            oversized=output/f"oversized-{mode}.out"
            compile_program([args.compiler,ROOT/"tests/cadkernel/nurbs_builder/oversized.dl",f"-{mode}","-o",oversized])
            for sweep in ("1686629710", "1e308"):
                status,text,_=COMMON["run_process"]([str(oversized),sweep],timeout=10,cwd=ROOT)
                assert status in (-6,134,3), (status,text)
                assert not text.strip(),text
                print(f"oversized/{mode}/{sweep}: expected resource refusal",flush=True)
    count=fields=0
    for name,values in inputs():
        if args.case not in name:continue
        arguments=[str(x) for x in values]
        (output/"current-input.json").write_text(json.dumps(dict(name=name,values=arguments)))
        _,reference,_=run([output/"reference-O0.out",*arguments],timeout=30)
        expected,limits=MEASURE["expected_values"](reference)
        _,optimized,_=run([output/"reference-O2.out",*arguments],timeout=30)
        optimized,_=MEASURE["expected_values"](optimized)
        assert len(optimized)==len(expected) and all(CURVE["equal"](a,b,exact=True) for a,b in zip(optimized,expected)),f"{name}: Rust optimization divergence"
        outputs=[]
        for mode in ("O0","O2"):
            _,text,_=run([output/f"probe-{mode}.out",*arguments],timeout=30)
            actual=CURVE["parsed"](text)
            (output/f"current-{mode}.txt").write_text(text)
            (output/"current-rust.txt").write_text(reference)
            assert len(actual)==len(expected),f"{name}/{mode}: {len(actual)} fields != {len(expected)}"
            for index,(a,b,limit) in enumerate(zip(actual,expected,limits)):
                if not CURVE["equal"](a,b,exact=limit<0,absolute=max(limit,0.0)):
                    raise AssertionError(f"{name}/{mode} field {index}: {a} != Rust {b}")
            outputs.append(actual)
        assert all(CURVE["equal"](a,b,exact=True) for a,b in zip(*outputs)),f"{name}: native optimization divergence"
        count+=1;fields+=len(expected)*2
        if count%25==0:print(f"{count} cases passed ({name})",flush=True)
    assert count and hashlib.sha256(args.compiler.read_bytes()).hexdigest()==digest
    report=dict(revision=CURVE["PIN"],compiler_sha256=digest,rustc=version,source_tests=0,native_groups=8,cases=count,fields=fields,case_filter=args.case,exact_native_optimization_parity=True,exact_rust_optimization_parity=True)
    report["source_sha256"]={p:hashlib.sha256((source/p).read_bytes()).hexdigest() for p in paths}
    report["native_source_sha256"]={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [ROOT/"lib/cadkernel/nurbs_builder.dl",ROOT/"tests/cadkernel/nurbs_builder_ownership/main.dl",*sorted((ROOT/"tests/cadkernel/nurbs_builder").glob("*.dl"))]}
    (output/("filtered-summary.json" if args.case else "summary.json")).write_text(json.dumps(report,indent=2)+"\n")
    print(f"PASS: {count} cases, {fields} Rust comparisons; artifacts={output}",flush=True)


if __name__=="__main__":main()
