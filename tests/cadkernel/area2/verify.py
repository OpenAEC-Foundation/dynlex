# SPDX-License-Identifier: MPL-2.0
"""Check signed area and both centroid conventions against pinned Rust."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import random
import re
import runpy
import tempfile

ROOT = Path(__file__).resolve().parents[3]
CROSS = runpy.run_path(str(ROOT / "tests/cadkernel/cross2/verify.py"))
CURVE, MEASURE, COMMON = (CROSS[key] for key in ("CURVE", "MEASURE", "COMMON"))
case, run, compile_program = (CROSS[key] for key in ("case", "run", "compile_program"))


def inputs():
    for kind in range(8):
        yield f"variant-{kind}", case(kind, knots=CURVE["clamped"](2,4), weights=[1.,.7,1.3,1.])
    for kind in (1,2,3):
        for radius in (-3.,-0.,0.,1e-150,1e-12,.5,7.,1e150,math.inf,math.nan):
            for origin in ((0.,0.),(512345.678,4512345.678)):
                yield f"radius-{kind}-{radius}-{origin}", case(kind,radius=radius,start=origin,first=0.,last=math.tau)
        for first,last in ((0.,0.),(0.,math.pi),(0.,math.tau),(1.,-1.),(-14.,18.),(math.nan,1.),(0.,math.inf)):
            yield f"sweep-{kind}-{first}-{last}",case(kind,first=first,last=last)
    for kind in (0,1,2,3,6,7):
        for scalar in (math.inf,-math.inf,math.nan,-0.):
            yield f"origin-{kind}-{scalar}",case(kind,start=(scalar,scalar))
    for closed in (False,True):
        for points in ([],[(2.,3.,0.)],[(0.,0.,0.),(0.,0.,0.)],
                       [(3.,4.,0.),(5.,4.,0.),(5.,6.,0.),(3.,6.,0.)],
                       [(3.,6.,0.),(5.,6.,0.),(5.,4.,0.),(3.,4.,0.)]):
            yield f"polyline-{closed}-{points}",case(4,closed=closed,vertices=points)
        for bulge in (-math.inf,-2.,-1.,-1e-12,-0.,0.,1e-12,1.,2.,math.inf,math.nan):
            yield f"bulge-{closed}-{bulge}",case(4,closed=closed,vertices=[(3.,4.,bulge),(5.,4.,0.),(5.,6.,0.)])
    for degree,points in ((1,[(0.,0.,0.),(2.,0.,0.),(2.,2.,0.),(0.,2.,0.),(0.,0.,0.)]),
                          (2,[(1.,0.,0.),(1.,1.,0.),(0.,1.,0.)]),
                          (2,[(0.,0.,0.),(1.,2.,0.),(3.,-1.,0.),(0.,0.,0.)])):
        for weights in ([],[1.]*len(points),[1. if i%2==0 else .7 for i in range(len(points))]):
            yield f"spline-{degree}-{points}-{weights}",case(5,degree=degree,vertices=points,knots=CURVE["clamped"](degree,len(points)),weights=weights)
    rng=random.Random(953546)
    for index in range(64):
        yield f"random-{index}",case(index%8,start=(rng.uniform(-50,50),rng.uniform(-50,50)),
            end=(rng.uniform(-5,5),rng.uniform(-5,5)),radius=rng.uniform(.1,8),minor=rng.uniform(.1,4),
            first=rng.uniform(-4,0),last=rng.uniform(1,6),knots=CURVE["clamped"](2,4),weights=[1.,.7,1.3,1.])


def rust_driver(source, output):
    driver=CROSS["rust_driver"](source,output)
    names=("transform","area","arrangement")
    run(["git","-c",f"safe.directory={source.as_posix()}","-C",source,"diff","--exit-code",CURVE["PIN"],"--",*[f"src/geom2d/{name}.rs" for name in names]])
    content=driver.read_text(encoding="utf-8")
    additions="".join(f'#[path=r"{(source/f"src/geom2d/{name}.rs").as_posix()}"] pub mod {name};\n' for name in names)
    content=content.replace("mod geom2d {\n","mod geom2d {\n"+additions+"pub use arrangement::signed_area;\n")
    content=content.replace("tests/cadkernel/cross2/reference.rs","tests/cadkernel/area2/reference.rs")
    driver.write_text(content,encoding="utf-8")
    return driver


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source",type=Path,required=True)
    parser.add_argument("--compiler",type=Path,default=ROOT/"build/dynlex.exe")
    parser.add_argument("--rustc",type=Path,default=Path.home()/".rustup/toolchains/stable-x86_64-pc-windows-msvc/bin/rustc.exe")
    parser.add_argument("--case",default="")
    parser.add_argument("--output",type=Path)
    parser.add_argument("--skip-build",action="store_true")
    args=parser.parse_args()
    output=args.output or Path(tempfile.mkdtemp(prefix="area2-",dir=ROOT/"build"))
    output.mkdir(exist_ok=True,parents=True)
    digest=hashlib.sha256(args.compiler.read_bytes()).hexdigest()
    _,version,_=run([args.rustc,"-vV"])
    if os.name=="nt" and "host: x86_64-pc-windows-msvc" not in version:
        raise AssertionError("MSVC reference required for native math parity")
    driver=rust_driver(args.source.resolve(),output)
    names=re.findall(r"#\[test\]\s*fn (\w+)",(args.source/"src/geom2d/area.rs").read_text(encoding="utf-8"))
    assert names==(ROOT/"tests/cadkernel/area2/expected.txt").read_text().splitlines() and len(names)==12
    if not args.skip_build:
        for mode in ("O0","O2"):
            options=[args.rustc,"--edition=2021","--crate-name","area_reference",driver,"-C",f"opt-level={mode[1]}","-A","dead_code","-A","unused_variables"]
            compile_program([*options,"-o",output/f"reference-{mode}.out"])
            upstream=output/f"upstream-{mode}.out"
            compile_program([*options,"--test","-o",upstream])
            _,text,_=run([upstream,"geom2d::area::tests::"])
            assert "12 passed; 0 failed" in text,text
            (output/f"upstream-{mode}.txt").write_text(text,encoding="utf-8")
            print(f"Rust {mode}: 12 source tests passed",flush=True)
            for fixture in ("area2","area2_centroid"):
                print(COMMON["verify_fixture"](ROOT/"tests/cadkernel"/fixture,mode,args.compiler,output,60,30,False),flush=True)
            compile_program([args.compiler,ROOT/"tests/cadkernel/area2/probe.dl",f"-{mode}","-o",output/f"probe-{mode}.out"])
    count=fields=0
    for name,shape in inputs():
        if args.case not in name: continue
        arguments=[str(x) for x in [len(shape),*shape]]
        (output/"current-input.json").write_text(json.dumps(dict(name=name,values=arguments)),encoding="utf-8")
        _,reference,_=run([output/"reference-O0.out",*arguments],timeout=30)
        expected,limits=MEASURE["expected_values"](reference)
        _,optimized,_=run([output/"reference-O2.out",*arguments],timeout=30)
        optimized,_=MEASURE["expected_values"](optimized)
        assert len(optimized)==len(expected) and all(CURVE["equal"](a,b,exact=True) for a,b in zip(optimized,expected)),f"{name}: Rust optimization divergence"
        outputs=[]
        for mode in ("O0","O2"):
            _,text,_=run([output/f"probe-{mode}.out",*arguments],timeout=30)
            actual=CURVE["parsed"](text)
            (output/f"current-{mode}.txt").write_text(text,encoding="utf-8")
            (output/"current-rust.txt").write_text(reference,encoding="utf-8")
            assert len(actual)==len(expected),f"{name}/{mode}: {len(actual)} fields != {len(expected)}"
            for index,(a,b,limit) in enumerate(zip(actual,expected,limits)):
                if not CURVE["equal"](a,b,exact=limit<0,absolute=max(0.,limit)):
                    raise AssertionError(f"{name}/{mode} field {index}: {a} != Rust {b}")
            outputs.append(actual)
        assert all(CURVE["equal"](a,b,exact=True) for a,b in zip(*outputs)),f"{name}: native optimization divergence"
        count+=1
        fields+=len(expected)*2
        if count%25==0: print(f"{count} cases passed ({name})",flush=True)
    assert count,"no cases selected"
    assert hashlib.sha256(args.compiler.read_bytes()).hexdigest()==digest,"compiler changed"
    report=dict(revision=CURVE["PIN"],compiler_sha256=digest,rustc=version,source_tests=12,cases=count,fields=fields,case_filter=args.case,exact_native_optimization_parity=True,exact_rust_optimization_parity=True)
    report["native_source_sha256"]={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [ROOT/"lib/cadkernel/area2.dl",*sorted((ROOT/"tests/cadkernel/area2").glob("*.dl"))]}
    (output/("filtered-summary.json" if args.case else "summary.json")).write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print(f"PASS: {count} cases, {fields} Rust comparisons; artifacts={output}",flush=True)


if __name__=="__main__":
    main()
