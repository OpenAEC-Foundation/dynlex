# SPDX-License-Identifier: MPL-2.0
"""Verify all endpoint length changes against the complete pinned Rust module."""
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
UPRIGHT=PLANAR["UPRIGHT"]


def case(name,mode,kind=1,amount=3.,point=(0.,0.,0.),pick=(2.,0.,0.),start=(0.,0.,0.),end=(2.,0.,0.),shape=None):
    return name,[mode,kind,amount,*point,*pick,*start,*end,*(shape if shape is not None else curve())]


def inputs():
    amounts=(-10.,-0.,1e-13,1e-12,1.00001e-12,1.,3.,100.,math.inf,math.nan)
    for kind in range(6):
        for amount in amounts:
            for pick in ((0.,0.,0.),(1.,0.,0.),(2.,0.,0.)):
                yield case(f"line-{kind}-{amount}-{pick}",0,kind,amount,point=(-3.,2.,1.),pick=pick)
    for value in (math.inf,math.nan,1e308,1e-15):
        for field in ("start","end","pick","point"):
            yield case(f"line-{field}-{value}",0,kind=5,**{field:(value,0.,0.)})
    for frame in (XY,UPRIGHT,[5.,-2.,7.,2.,0.,0.,1.,3.,0.],[0.]*9):
        for mode,shape_kind in ((1,2),(2,3)):
            for kind in range(6):
                for pick in ((5.,-2.,7.),(-2.,2.,0.)):
                    yield case(f"planar-{mode}-{frame}-{kind}-{pick}",mode,kind,amount=1.25,
                        point=(-4.,1.,3.),pick=pick,shape=curve(shape_kind,frame=frame,radius=4.,minor=2.))
    for mode,shape_kind in ((1,2),(2,3)):
        for first,last in ((0.,0.),(0.,math.tau),(5.,1.),(-7.,-2.),(0.,math.inf),(math.nan,2.)):
            for kind in (1,3,4,5):
                yield case(f"angles-{mode}-{first}-{last}-{kind}",mode,kind,amount=math.pi,
                    point=(-2.,0.,0.),shape=curve(shape_kind,first=first,last=last))
        for value in (-1.,0.,1e-13,1e-12,1e200,math.inf,math.nan):
            for key in ("radius","minor"):
                yield case(f"radius-{mode}-{key}-{value}",mode,shape=curve(shape_kind,**{key:value}))
            for field in ("pick","point"):
                yield case(f"planar-invalid-{mode}-{field}-{value}",mode,kind=5,shape=curve(shape_kind),**{field:(value,0.,0.)})
        for amount in amounts+(math.tau-2e-12,math.tau-1e-12,math.tau,math.tau+1.):
            for kind in (0,1,2,3,4):
                yield case(f"amount-{mode}-{kind}-{amount}",mode,kind,amount,shape=curve(shape_kind))
        for point in ((0.,0.,0.),(1e-13,0.,0.),(1e-12,0.,0.),(0.,-2.,0.),(-0.,-2.,0.)):
            yield case(f"dynamic-{mode}-{point}",mode,5,point=point,shape=curve(shape_kind))
        for kind in (0,1,4,6,7):
            yield case(f"wrong-type-{mode}-{kind}",mode,shape=curve(kind))
    chains=(
        ((0.,0.,0.),(2.,0.,0.),(5.,0.,.75)),
        ((0.,0.,1.),(2.,0.,0.),(5.,1.,0.)),
        ((0.,0.,-.5),(2.,0.,1.),(5.,1.,0.)),
        ((0.,0.,1.),(2.,0.,0.)),
        ((0.,0.,0.),(0.,0.,0.)),
        ((0.,0.,0.),),(),
        ((0.,0.,math.inf),(2.,0.,0.)),
        ((math.nan,0.,0.),(2.,0.,0.)),
        ((0.,0.,0.),(1e-13,0.,0.)),
    )
    for n,points in enumerate(chains):
        for kind in range(6):
            for amount in (.25,1.,3.,7.,math.tau,100.):
                for pick in ((0.,0.,0.),(5.,0.,0.)):
                    yield case(f"polyline-{n}-{kind}-{amount}-{pick}",3,kind,amount,pick=pick,
                        point=(7.,1.,0.),shape=curve(4,points=points))
    for closed in (False,True):
        for amount in amounts:
            yield case(f"polyline-target-{closed}-{amount}",3,amount=amount,shape=curve(4,closed=closed))
    for shape_kind in (0,1,2,3,6,7):
        yield case(f"polyline-wrong-type-{shape_kind}",3,shape=curve(shape_kind))
    import random
    rng=random.Random(64027)
    for index in range(80):
        kind=rng.randrange(3)
        points=[(rng.uniform(-5,5),rng.uniform(-5,5),rng.choice((0.,.3,-.7,1.))) for _ in range(rng.randrange(2,8))]
        yield case(f"random-polyline-{index}",3,kind,rng.uniform(.01,20) if kind!=2 else rng.uniform(.01,400),
            pick=(rng.uniform(-5,5),rng.uniform(-5,5),0.),shape=curve(4,points=points))


def rust_driver(source,output):
    space=("spline","vec","plane","planar","lengthen")
    geom=("curve","polyline","vec","angle","nurbs","deviation","arclength")
    paths=["src/tessellation.rs","src/geom2d/mod.rs",*[f"src/space/{n}.rs" for n in space],*[f"src/geom2d/{n}.rs" for n in geom]]
    _,revision,_=run(["git","-c",f"safe.directory={source.as_posix()}","-C",source,"rev-parse","HEAD"])
    assert revision.strip()==CURVE["PIN"]
    run(["git","-c",f"safe.directory={source.as_posix()}","-C",source,"diff","--exit-code",CURVE["PIN"],"--",*paths])
    def module(name,path):return f'#[path=r"{(source/path).as_posix()}"] pub mod {name};\n'
    contents=(source/"src/geom2d/mod.rs").read_text(encoding="utf-8")
    start=contents.rfind("#[derive",0,contents.index("pub struct Ellipse"))
    end=contents.index("/// Distance below which",start)
    content=module("tessellation","src/tessellation.rs")+"mod space {\n"
    content+="".join(module(n,f"src/space/{n}.rs") for n in space)
    content+="pub use vec::Vec3;pub use planar::PlanarCurve;\n}\nmod geom2d {\n"
    content+="".join(module(n,f"src/geom2d/{n}.rs") for n in geom)
    content+="pub use vec::Vec2;pub use curve::*;pub use nurbs::NurbsCurve;pub use polyline::{Polyline,PolylineVertex,BulgeArc};\n"+contents[start:end]+"\n}\n"
    content+=f'include!(r"{(ROOT/"tests/cadkernel/lengthen3/reference.rs").as_posix()}");\n'
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
    output=args.output or Path(tempfile.mkdtemp(prefix="lengthen3-",dir=ROOT/"build"))
    output.mkdir(parents=True,exist_ok=True)
    digest=hashlib.sha256(args.compiler.read_bytes()).hexdigest()
    _,version,_=run([args.rustc,"-vV"])
    if os.name=="nt":assert "host: x86_64-pc-windows-msvc" in version
    source=args.source.resolve()
    driver,paths=rust_driver(source,output)
    names=re.findall(r"#\[test\]\s*fn (\w+)",(source/"src/space/lengthen.rs").read_text())
    assert names==(ROOT/"tests/cadkernel/lengthen3/expected.txt").read_text().splitlines() and len(names)==5
    if not args.skip_build:
        for mode in ("O0","O2"):
            options=[args.rustc,"--edition=2021","--cfg",'feature="geom2d"',"--crate-name","lengthen_reference",driver,"-C",f"opt-level={mode[1]}","-A","dead_code"]
            compile_program([*options,"-o",output/f"reference-{mode}.out"])
            compile_program([*options,"--test","-o",output/f"upstream-{mode}.out"])
            _,text,_=run([output/f"upstream-{mode}.out","space::lengthen::tests::"])
            assert "5 passed; 0 failed" in text,text
            (output/f"upstream-{mode}.txt").write_text(text)
            for fixture in ("lengthen3", "lengthen3_ownership"):
                print(COMMON["verify_fixture"](ROOT/"tests/cadkernel"/fixture,mode,args.compiler,output,60,30,False),flush=True)
            compile_program([args.compiler,ROOT/"tests/cadkernel/lengthen3/probe.dl",f"-{mode}","-o",output/f"probe-{mode}.out"])
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
                if not CURVE["equal"](a,b,exact=limit<0):
                    raise AssertionError(f"{name}/{mode} field {index}: {a} != Rust {b}")
            outputs.append(actual)
        assert all(CURVE["equal"](a,b,exact=True) for a,b in zip(*outputs)),f"{name}: native optimization divergence"
        count+=1;fields+=len(expected)*2
        if count%25==0:print(f"{count} cases passed ({name})",flush=True)
    assert count and hashlib.sha256(args.compiler.read_bytes()).hexdigest()==digest
    report=dict(revision=CURVE["PIN"],compiler_sha256=digest,rustc=version,source_tests=5,cases=count,fields=fields,case_filter=args.case,exact_native_optimization_parity=True,exact_rust_optimization_parity=True)
    report["source_sha256"]={p:hashlib.sha256((source/p).read_bytes()).hexdigest() for p in paths}
    report["native_source_sha256"]={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [ROOT/"lib/cadkernel/lengthen3.dl",*sorted((ROOT/"tests/cadkernel/lengthen3").glob("*.dl"))]}
    (output/("filtered-summary.json" if args.case else "summary.json")).write_text(json.dumps(report,indent=2)+"\n")
    print(f"PASS: {count} cases, {fields} Rust comparisons; artifacts={output}",flush=True)


if __name__=="__main__":main()
