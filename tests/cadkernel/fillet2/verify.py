# SPDX-License-Identifier: MPL-2.0
"""Compare both fillet constructions with unchanged pinned Rust."""
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


shape=CURVE["case"]


def rays(name,radius,apex=(0.,0.),first=(1.,0.),second=(0.,1.)):
    return name,[0,radius,1e-9,*apex,*first,*second]


def pair(name,first,second,radius=1.,tolerance=1e-9):
    return name,[1,radius,tolerance,0.,0.,1.,0.,0.,1.,len(first),*first,len(second),*second]


def inputs():
    for angle in (0.,1e-7,0.999e-6,1.001e-6,.01,.5,math.pi/2,math.pi-1.001e-6,math.pi-0.999e-6,math.pi,4.,6.):
        for radius in (-1.,0.,.999e-9,1e-9,.5,3.,1e150,math.inf,math.nan):
            yield rays(f"rays-{angle}-{radius}",radius,second=(math.cos(angle),math.sin(angle)))
    for apex in ((512345.678,4512345.678),(-0.,0.),(1e15,-1e15),(math.inf,0.),(math.nan,0.)):
        for radius in (.75,2.,1e-12):
            yield rays(f"apex-{apex}-{radius}",radius,apex)
    for first in ((0.,0.),(2.,0.),(.6,.8),(math.nan,0.),(math.inf,0.),(1e-305,0.)):
        for second in ((0.,0.),(0.,1.),(-1.,0.),(math.nan,math.nan)):
            yield rays(f"nonunit-{first}-{second}",1.,first=first,second=second)
    for left in range(8):
        for right in range(8):
            first=shape(left,start=(-3.,-1.),end=(2.,-1.),knots=CURVE["clamped"](2,4),weights=[1.,.7,1.3,1.])
            second=shape(right,start=(3.,1.),end=(3.,-2.),knots=CURVE["clamped"](2,4),weights=[1.,.7,1.3,1.])
            for radius in (.5,3.,15.):
                yield pair(f"dispatch-{left}-{right}-{radius}",first,second,radius)
    for own in (-2.,0.,1e-13,1.,2.,1e150,math.inf,math.nan):
        for radius in (.5,1.,2.,3.,math.inf,math.nan,-1.,0.):
            yield pair(f"circle-radius-{own}-{radius}",shape(1,start=(0.,0.),radius=own),
                shape(0,start=(-50.,-2.),end=(50.,-2.)),radius)
    for tolerance in (1e-15,1e-9,1e-3,.5):
        for gap in (-1e-10,0.,1e-10,1.,10.):
            yield pair(f"tangency-{gap}-{tolerance}",shape(1,radius=10.),
                shape(0,start=(-50.,-10.+gap),end=(50.,-10.+gap)),3.,tolerance)
    for own in (1.,5.,10.):
        for other in (1.,5.,10.):
            for distance in (0.,2.,10.,30.):
                yield pair(f"circle-pair-{own}-{other}-{distance}",shape(1,radius=own),
                    shape(1,start=(distance,0.),radius=other),5.)
    for value in (0.,1e-305,1e-150,1e150,math.inf,math.nan):
        yield pair(f"degenerate-line-{value}",shape(0,end=(value,0.)),shape(0,end=(0.,1.)),1.)
    import random
    rng=random.Random(98037)
    for index in range(100):
        def random_shape():
            return shape(rng.choice((0,1,2,6,7)),start=(rng.uniform(-10,10),rng.uniform(-10,10)),
                end=(rng.uniform(-10,10),rng.uniform(-10,10)),radius=rng.uniform(.01,10.),
                first=rng.uniform(-5,5),last=rng.uniform(-5,5))
        yield pair(f"random-{index}",random_shape(),random_shape(),rng.uniform(.01,10.),1e-7)


def rust_driver(source,output):
    driver=CROSS["rust_driver"](source,output)
    path="src/geom2d/fillet.rs"
    run(["git","-c",f"safe.directory={source.as_posix()}","-C",source,"diff","--exit-code",CURVE["PIN"],"--",path])
    content=driver.read_text().replace("mod geom2d {\n",f'mod geom2d {{\n#[path=r"{(source/path).as_posix()}"] pub mod fillet;\n')
    content=content.replace("tests/cadkernel/cross2/reference.rs","tests/cadkernel/fillet2/reference.rs")
    driver.write_text(content)
    paths=re.findall(r'#\[path=r"([^"]+)"\]',content)
    paths=[str(Path(p).relative_to(source)).replace("\\","/") for p in paths]+["src/geom2d/mod.rs"]
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
    output=args.output or Path(tempfile.mkdtemp(prefix="fillet2-",dir=ROOT/"build"))
    output.mkdir(parents=True,exist_ok=True)
    digest=hashlib.sha256(args.compiler.read_bytes()).hexdigest()
    _,version,_=run([args.rustc,"-vV"])
    if os.name=="nt":assert "host: x86_64-pc-windows-msvc" in version
    source=args.source.resolve()
    driver,paths=rust_driver(source,output)
    names=re.findall(r"#\[test\]\s*fn (\w+)",(source/"src/geom2d/fillet.rs").read_text())
    assert names==(ROOT/"tests/cadkernel/fillet2/expected.txt").read_text().splitlines() and len(names)==20
    if not args.skip_build:
        for mode in ("O0","O2"):
            options=[args.rustc,"--edition=2021","--crate-name","fillet_reference",driver,"-C",f"opt-level={mode[1]}","-A","dead_code"]
            compile_program([*options,"-o",output/f"reference-{mode}.out"])
            compile_program([*options,"--test","-o",output/f"upstream-{mode}.out"])
            _,text,_=run([output/f"upstream-{mode}.out","geom2d::fillet::tests::"])
            assert "20 passed; 0 failed" in text,text
            (output/f"upstream-{mode}.txt").write_text(text)
            for fixture in ("fillet2", "fillet2_ownership"):
                print(COMMON["verify_fixture"](ROOT/"tests/cadkernel"/fixture,mode,args.compiler,output,60,30,False),flush=True)
            compile_program([args.compiler,ROOT/"tests/cadkernel/fillet2/probe.dl",f"-{mode}","-o",output/f"probe-{mode}.out"])
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
    report=dict(revision=CURVE["PIN"],compiler_sha256=digest,rustc=version,source_tests=20,cases=count,fields=fields,case_filter=args.case,exact_native_optimization_parity=True,exact_rust_optimization_parity=True)
    report["source_sha256"]={p:hashlib.sha256((source/p).read_bytes()).hexdigest() for p in paths}
    report["native_source_sha256"]={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [ROOT/"lib/cadkernel/fillet2.dl",*sorted((ROOT/"tests/cadkernel/fillet2").glob("*.dl"))]}
    (output/("filtered-summary.json" if args.case else "summary.json")).write_text(json.dumps(report,indent=2)+"\n")
    print(f"PASS: {count} cases, {fields} Rust comparisons; artifacts={output}",flush=True)


if __name__=="__main__":main()
