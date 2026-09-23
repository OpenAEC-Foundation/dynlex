# SPDX-License-Identifier: MPL-2.0
"""Verify precision-bounded spline polylines against unchanged Rust."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import random
import runpy
import tempfile

ROOT=Path(__file__).resolve().parents[3]
CROSS=runpy.run_path(str(ROOT/"tests/cadkernel/cross2/verify.py"))
CURVE,MEASURE,COMMON=(CROSS[key] for key in ("CURVE","MEASURE","COMMON"))
run,compile_program=(CROSS[key] for key in ("run","compile_program"))


def shape(points, degree=2, knots=None, weights=None, closed=False):
    count=len(points)
    knots=knots if knots is not None else CURVE["clamped"](degree,count)
    weights=weights if weights is not None else [1.]*count
    return [degree,count,len(knots),len(weights),int(closed),*[x for p in points for x in p],*knots,*weights]


def inputs():
    original=[(0.,0.,0.),(1.,3.,1.),(2.,-1.,2.),(4.,0.,3.)]
    for precision in (0,1,5,20,50,99,100,255):
        for weights in ([1.,1.,1.,1.],[1.,3.,.5,2.]):
            yield f"precision-{precision}-{weights}",[precision,*shape(original,knots=[0.,0.,0.,.4,1.,1.,1.],weights=weights)]
    for degree in (1,2,3,8,32,64,65):
        points=[(i/degree,(i%2)*.25,(i%3)*.125) for i in range(degree+1)]
        for precision in (0,10):
            yield f"degree-{degree}-{precision}",[precision,*shape(points,degree)]
    for magnitude in (0.,1e-150,1e150,1e308):
        points=[tuple(x*magnitude for x in p) for p in original]
        yield f"scale-{magnitude}",[20,*shape(points)]
    for weight in (-1.,-0.,0.,1e-300,1e300,math.inf,math.nan):
        yield f"weight-{weight}",[10,*shape(original,weights=[1.,weight,1.,1.])]
    for scalar in (math.inf,-math.inf,math.nan):
        points=original.copy();points[1]=(scalar,3.,1.)
        yield f"coordinate-{scalar}",[10,*shape(points)]
    yield "discontinuous",[10,*shape([(x,x,x) for x in (0.,1.,2.,4.,5.,6.)],knots=[0.,0.,0.,.5,.5,.5,1.,1.,1.])]
    yield "unclamped",[10,*shape(original,knots=[0.,.1,.2,.4,.6,.8,1.])]
    yield "collapsed-domain",[10,*shape(original,knots=[0.]*7)]
    yield "closed",[10,*shape(original,closed=True)]
    rng=random.Random(953546)
    for index in range(40):
        degree=1+index%4;count=degree+1+index%3
        points=[(rng.uniform(-3,3),rng.uniform(-3,3),rng.uniform(-3,3)) for _ in range(count)]
        weights=[rng.uniform(.1,4.) for _ in range(count)]
        yield f"random-{index}",[index%21,*shape(points,degree,weights=weights)]


def rust_driver(source,output):
    paths=["src/space/"+n+".rs" for n in ("polyline_approximation","nurbs","spline","vec")]+["src/tessellation.rs"]
    _,revision,_=run(["git","-c",f"safe.directory={source.as_posix()}","-C",source,"rev-parse","HEAD"])
    assert revision.strip()==CURVE["PIN"]
    run(["git","-c",f"safe.directory={source.as_posix()}","-C",source,"diff","--exit-code",CURVE["PIN"],"--",*paths])
    def module(name,path):return f'#[path=r"{(source/path).as_posix()}"] pub mod {name};\n'
    content=module("tessellation","src/tessellation.rs")+"mod space {\n"
    content+="".join(module(name,f"src/space/{name}.rs") for name in ("vec","spline","nurbs","polyline_approximation"))
    content+="pub use vec::Vec3;pub use nurbs::NurbsCurve3;\n}\n"
    content+=f'include!(r"{(ROOT/"tests/cadkernel/polyline_approximation3/reference.rs").as_posix()}");\n'
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
    output=args.output or Path(tempfile.mkdtemp(prefix="approximation3-",dir=ROOT/"build"))
    output.mkdir(parents=True,exist_ok=True)
    digest=hashlib.sha256(args.compiler.read_bytes()).hexdigest()
    _,version,_=run([args.rustc,"-vV"])
    if os.name=="nt":assert "host: x86_64-pc-windows-msvc" in version
    source=args.source.resolve()
    driver,paths=rust_driver(source,output)
    names=re.findall(r"#\[test\]\s*fn (\w+)",(source/"src/space/polyline_approximation.rs").read_text())
    assert names==(ROOT/"tests/cadkernel/polyline_approximation3/expected.txt").read_text().splitlines() and len(names)==2
    if not args.skip_build:
        for mode in ("O0","O2"):
            options=[args.rustc,"--edition=2021","--crate-name","join_reference",driver,"-C",f"opt-level={mode[1]}","-A","dead_code"]
            compile_program([*options,"-o",output/f"reference-{mode}.out"])
            compile_program([*options,"--test","-o",output/f"upstream-{mode}.out"])
            _,text,_=run([output/f"upstream-{mode}.out","space::polyline_approximation::tests::"])
            assert "2 passed; 0 failed" in text,text
            (output/f"upstream-{mode}.txt").write_text(text)
            for fixture in ("polyline_approximation3", "polyline_approximation3_ownership"):
                print(COMMON["verify_fixture"](ROOT/"tests/cadkernel"/fixture,mode,args.compiler,output,60,30,False),flush=True)
            compile_program([args.compiler,ROOT/"tests/cadkernel/polyline_approximation3/probe.dl",f"-{mode}","-o",output/f"probe-{mode}.out"])
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
    report=dict(revision=CURVE["PIN"],compiler_sha256=digest,rustc=version,source_tests=2,cases=count,fields=fields,case_filter=args.case,exact_native_optimization_parity=True,exact_rust_optimization_parity=True)
    report["source_sha256"]={p:hashlib.sha256((source/p).read_bytes()).hexdigest() for p in paths}
    report["native_source_sha256"]={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [ROOT/"lib/cadkernel/polyline_approximation3.dl",*sorted((ROOT/"tests/cadkernel/polyline_approximation3").glob("*.dl"))]}
    (output/("filtered-summary.json" if args.case else "summary.json")).write_text(json.dumps(report,indent=2)+"\n")
    print(f"PASS: {count} cases, {fields} Rust comparisons; artifacts={output}",flush=True)


if __name__=="__main__":main()
