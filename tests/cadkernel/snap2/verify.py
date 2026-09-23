# SPDX-License-Identifier: MPL-2.0
"""Compare exact curve snap candidates with the complete pinned Rust module."""
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


def case(name,mode,curve,target=(4.,6.)):
    return name,[mode,*target,len(curve),*curve]


def inputs():
    for kind in range(8):
        curve=shape(kind,knots=CURVE["clamped"](2,4),weights=[1.,.7,1.3,1.])
        for mode in range(4):
            for target in ((0.,0.),(1.,1.),(4.,6.),(-1e-9,0.),(-10.,10.),(1e150,1e150),(math.nan,0.),(math.inf,0.)):
                yield case(f"kind-{kind}-mode-{mode}-target-{target}",mode,curve,target)
    for kind in (1,2,3):
        for radius in (-2.,-0.,0.,1e-200,1e200,math.inf,math.nan):
            for mode in range(4):
                yield case(f"radius-{kind}-{radius}-{mode}",mode,shape(kind,radius=radius))
    for kind in (2,3):
        for first,last in ((0.,0.),(0.,math.pi/2),(0.,math.pi),(0.,math.tau),(5.,1.),(-20.,30.),(math.nan,1.),(0.,math.inf)):
            for mode in range(4):
                yield case(f"span-{kind}-{first}-{last}-{mode}",mode,shape(kind,first=first,last=last))
    for axis in ((1.,0.),(0.,1.),(.6,.8),(2.,3.),(0.,0.)):
        for minor in (0.,1.,4.):
            for mode in range(4):
                yield case(f"ellipse-axis-{axis}-{minor}-{mode}",mode,shape(3,axis=axis,minor=minor,first=.2,last=5.7))
    for closed in (False,True):
        for points in ((),((1.,2.,0.),),((0.,0.,0.),(2.,0.,0.),(2.,2.,0.)),
            ((0.,0.,1.),(2.,0.,-.5),(2.,2.,0.)),((0.,0.,0.),(0.,0.,0.))):
            for mode in range(4):
                yield case(f"polyline-{closed}-{points}-{mode}",mode,shape(4,vertices=points,closed=closed))
    for kind in (0,6,7):
        for x in (-1.00001e-9,-1e-9,-.5e-9,0.,1.,1.+.5e-9,1.+1e-9,1.+1.00001e-9):
            yield case(f"extent-{kind}-{x}",1,shape(kind,end=(1.,0.)),(x,3.))
    for kind in (1,2):
        for x in (0.,2.-1e-12,2.,2.+.5e-12,2.+1e-12,2.+2e-12,5.):
            yield case(f"tangent-radius-boundary-{kind}-{x}",2,shape(kind,radius=2.,first=0.,last=math.pi/2),(x,0.))
    for kind in (1,2,3):
        for mode in range(4):
            yield case(f"survey-{kind}-{mode}",mode,shape(kind,start=(512345.678,4512345.678),radius=2.),(512349.678,4512348.678))
    import random
    rng=random.Random(987261)
    for index in range(50):
        kind=rng.randrange(8)
        curve=shape(kind,start=(rng.uniform(-5,5),rng.uniform(-5,5)),end=(rng.uniform(-5,5),rng.uniform(-5,5)),
            radius=rng.uniform(.1,6),minor=rng.uniform(.1,4),first=rng.uniform(-6,6),last=rng.uniform(-6,6),
            vertices=[(rng.uniform(-5,5),rng.uniform(-5,5),rng.choice((0.,.5,-1.))) for _ in range(4)],
            knots=CURVE["clamped"](2,4),weights=[1.,.7,1.3,1.])
        target=(rng.uniform(-10,10),rng.uniform(-10,10))
        for mode in range(4):
            yield case(f"random-{index}-{mode}",mode,curve,target)


def rust_driver(source,output):
    driver=CROSS["rust_driver"](source,output)
    path="src/geom2d/snap.rs"
    run(["git","-c",f"safe.directory={source.as_posix()}","-C",source,"diff","--exit-code",CURVE["PIN"],"--",path])
    content=driver.read_text().replace("mod geom2d {\n",f'mod geom2d {{\n#[path=r"{(source/path).as_posix()}"] pub mod snap;\n')
    content=content.replace("tests/cadkernel/cross2/reference.rs","tests/cadkernel/snap2/reference.rs")
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
    output=args.output or Path(tempfile.mkdtemp(prefix="snap2-",dir=ROOT/"build"))
    output.mkdir(parents=True,exist_ok=True)
    digest=hashlib.sha256(args.compiler.read_bytes()).hexdigest()
    _,version,_=run([args.rustc,"-vV"])
    if os.name=="nt":assert "host: x86_64-pc-windows-msvc" in version
    source=args.source.resolve()
    driver,paths=rust_driver(source,output)
    names=re.findall(r"#\[test\]\s*fn (\w+)",(source/"src/geom2d/snap.rs").read_text())
    assert names==(ROOT/"tests/cadkernel/snap2/expected.txt").read_text().splitlines() and len(names)==16
    if not args.skip_build:
        for mode in ("O0","O2"):
            options=[args.rustc,"--edition=2021","--crate-name","snap_reference",driver,"-C",f"opt-level={mode[1]}","-A","dead_code"]
            compile_program([*options,"-o",output/f"reference-{mode}.out"])
            compile_program([*options,"--test","-o",output/f"upstream-{mode}.out"])
            _,text,_=run([output/f"upstream-{mode}.out","geom2d::snap::tests::"])
            assert "16 passed; 0 failed" in text,text
            (output/f"upstream-{mode}.txt").write_text(text)
            for fixture in ("snap2", "snap2_ownership"):
                print(COMMON["verify_fixture"](ROOT/"tests/cadkernel"/fixture,mode,args.compiler,output,60,30,False),flush=True)
            compile_program([args.compiler,ROOT/"tests/cadkernel/snap2/probe.dl",f"-{mode}","-o",output/f"probe-{mode}.out"])
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
    report=dict(revision=CURVE["PIN"],compiler_sha256=digest,rustc=version,source_tests=16,cases=count,fields=fields,case_filter=args.case,exact_native_optimization_parity=True,exact_rust_optimization_parity=True)
    report["source_sha256"]={p:hashlib.sha256((source/p).read_bytes()).hexdigest() for p in paths}
    report["native_source_sha256"]={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [ROOT/"lib/cadkernel/snap2.dl",*sorted((ROOT/"tests/cadkernel/snap2").glob("*.dl"))]}
    (output/("filtered-summary.json" if args.case else "summary.json")).write_text(json.dumps(report,indent=2)+"\n")
    print(f"PASS: {count} cases, {fields} Rust comparisons; artifacts={output}",flush=True)


if __name__=="__main__":main()
