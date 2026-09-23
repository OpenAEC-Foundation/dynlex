# SPDX-License-Identifier: MPL-2.0
"""Verify complete finite-segment centre-line construction against pinned Rust."""
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


def case(name,first=(0.,0.,4.,0.),second=(6.,2.,10.,2.),pick1=(4.,0.),pick2=(10.,2.),extensions=(1.,2.)):
    return name,[*first,*second,*pick1,*pick2,*extensions]


def inputs():
    for first in ((0.,0.,4.,0.),(4.,0.,0.,0.),(-2.,0.,2.,0.)):
        for second in ((6.,2.,10.,2.),(10.,2.,6.,2.),(0.,2.,4.,2.),(0.,0.,4.,0.),(0.,-2.,0.,2.),(-2.,-2.,2.,2.)):
            for pick1 in ((4.,0.),(-4.,0.),(0.,0.)):
                for pick2 in ((0.,3.),(0.,-3.)):
                    for extensions in ((0.,0.),(1.,2.),(-1.,.5),(-10.,-10.)):
                        yield case(f"segments-{first}-{second}-{pick1}-{pick2}-{extensions}",first,second,pick1,pick2,extensions)
    for angle in (0.,1e-12,.99999e-10,1.00001e-10,1e-8,.4,math.pi-1e-12):
        yield case(f"near-parallel-{angle}",second=(0.,2.,4.*math.cos(angle),2.+4.*math.sin(angle)))
    for length in (0.,1e-300,1e-11,1e-10,1.000001e-10,1e-9,1e150,1e200):
        yield case(f"length-{length}",first=(0.,0.,length,0.))
    for value in (math.inf,-math.inf,math.nan,1e308,-0.):
        original=list(case("base")[1])
        for index in range(14):
            values=original.copy();values[index]=value
            yield f"nonfinite-{index}-{value}",values
    for start in (-10.,-9.,-8.,0.,1.,10.):
        for end in (-10.,-2.,0.,1.,10.):
            yield case(f"extensions-{start}-{end}",extensions=(start,end))
    for origin in ((512345.678,4512345.678),(1e12,-1e12),(-0.,-0.)):
        ox,oy=origin
        yield case(f"translated-parallel-{origin}",first=(ox,oy,ox+4,oy),second=(ox+6,oy+2,ox+10,oy+2),pick1=(ox+4,oy),pick2=(ox+10,oy+2))
        yield case(f"translated-corner-{origin}",first=(ox-2,oy,ox+2,oy),second=(ox,oy-2,ox,oy+2),pick1=(ox+2,oy),pick2=(ox,oy+2))
    import random
    rng=random.Random(729173)
    for index in range(100):
        yield f"random-{index}",[rng.uniform(-10,10) for _ in range(12)]+[rng.uniform(-5,5),rng.uniform(-5,5)]


def rust_driver(source,output):
    driver=CROSS["rust_driver"](source,output)
    path="src/geom2d/centerline.rs"
    run(["git","-c",f"safe.directory={source.as_posix()}","-C",source,"diff","--exit-code",CURVE["PIN"],"--",path])
    content=driver.read_text().replace("mod geom2d {\n",f'mod geom2d {{\n#[path=r"{(source/path).as_posix()}"] pub mod centerline;\n')
    content=content.replace("tests/cadkernel/cross2/reference.rs","tests/cadkernel/centerline2/reference.rs")
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
    output=args.output or Path(tempfile.mkdtemp(prefix="centerline2-",dir=ROOT/"build"))
    output.mkdir(parents=True,exist_ok=True)
    digest=hashlib.sha256(args.compiler.read_bytes()).hexdigest()
    _,version,_=run([args.rustc,"-vV"])
    if os.name=="nt":assert "host: x86_64-pc-windows-msvc" in version
    source=args.source.resolve()
    driver,paths=rust_driver(source,output)
    names=re.findall(r"#\[test\]\s*fn (\w+)",(source/"src/geom2d/centerline.rs").read_text())
    assert names==[], "source module has no unit tests"
    assert len((ROOT/"tests/cadkernel/centerline2/expected.txt").read_text().splitlines())==6
    if not args.skip_build:
        for mode in ("O0","O2"):
            options=[args.rustc,"--edition=2021","--crate-name","centerline_reference",driver,"-C",f"opt-level={mode[1]}","-A","dead_code"]
            compile_program([*options,"-o",output/f"reference-{mode}.out"])
            for fixture in ("centerline2",):
                print(COMMON["verify_fixture"](ROOT/"tests/cadkernel"/fixture,mode,args.compiler,output,60,30,False),flush=True)
            compile_program([args.compiler,ROOT/"tests/cadkernel/centerline2/probe.dl",f"-{mode}","-o",output/f"probe-{mode}.out"])
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
    report=dict(revision=CURVE["PIN"],compiler_sha256=digest,rustc=version,source_tests=0,native_groups=6,cases=count,fields=fields,case_filter=args.case,exact_native_optimization_parity=True,exact_rust_optimization_parity=True)
    report["source_sha256"]={p:hashlib.sha256((source/p).read_bytes()).hexdigest() for p in paths}
    report["native_source_sha256"]={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [ROOT/"lib/cadkernel/centerline2.dl",*sorted((ROOT/"tests/cadkernel/centerline2").glob("*.dl"))]}
    (output/("filtered-summary.json" if args.case else "summary.json")).write_text(json.dumps(report,indent=2)+"\n")
    print(f"PASS: {count} cases, {fields} Rust comparisons; artifacts={output}",flush=True)


if __name__=="__main__":main()
