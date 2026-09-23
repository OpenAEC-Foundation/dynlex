# SPDX-License-Identifier: MPL-2.0
"""Check planar arc sampling and interpolation against unchanged Rust."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import runpy
import tempfile

ROOT = Path(__file__).resolve().parents[3]
CROSS = runpy.run_path(str(ROOT / "tests/cadkernel/cross2/verify.py"))
CURVE, MEASURE, COMMON = (CROSS[key] for key in ("CURVE", "MEASURE", "COMMON"))
run, compile_program = (CROSS[key] for key in ("run", "compile_program"))


def inputs():
    for kind in (0,1):
        for density in (-math.inf,-1.,-0.,0.,.5,1.,20.,80.,math.inf,math.nan):
            for start,end in ((0.,math.tau),(1.,1.000001),(2.,-.5)):
                yield f"density-{kind}-{density}-{start}-{end}",[kind,3.,-4.,7.5,3.,.6,.8,start,end,2.,density]
        for radius in (-3.,-0.,0.,1e-150,1e150,math.inf,math.nan):
            for centre in ((0.,0.),(512345.678,4512345.678)):
                yield f"radius-{kind}-{radius}-{centre}",[kind,*centre,radius,radius,1.,0.,0.,math.tau,-0.,20.]
        for start,end in ((0.,0.),(-20.,25.),(math.inf,1.),(0.,math.nan)):
            yield f"sweep-{kind}-{start}-{end}",[kind,0.,0.,2.,1.,1.,0.,start,end,math.inf,3.]
    for axis in ((0.,0.),(.6,.8),(2.,3.),(-1.,0.)):
        yield f"ellipse-axis-{axis}",[1,-6.,11.,8.,3.,*axis,.2,5.,9.,20.]
    for t in (-1.,-0.,0.,.37,.5,1.,2.,math.inf,math.nan):
        yield f"lerp-{t}",[2,1.,2.,10.,20.,t]
        yield f"tiny-lerp-{t}",[2,1e-150,-1e-150,2e-150,3e-150,t]


def rust_driver(source, output):
    driver=CROSS["rust_driver"](source,output)
    names=("tessellate",)
    run(["git","-c",f"safe.directory={source.as_posix()}","-C",source,"diff","--exit-code",CURVE["PIN"],"--",*[f"src/geom2d/{name}.rs" for name in names]])
    content=driver.read_text(encoding="utf-8")
    additions="".join(f'#[path=r"{(source/f"src/geom2d/{name}.rs").as_posix()}"] pub mod {name};\n' for name in names)
    content=content.replace("mod geom2d {\n","mod geom2d {\n"+additions)
    content=content.replace("tests/cadkernel/cross2/reference.rs","tests/cadkernel/tessellate2/reference.rs")
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
    output=args.output or Path(tempfile.mkdtemp(prefix="tessellate2-",dir=ROOT/"build"))
    output.mkdir(exist_ok=True,parents=True)
    digest=hashlib.sha256(args.compiler.read_bytes()).hexdigest()
    _,version,_=run([args.rustc,"-vV"])
    if os.name=="nt" and "host: x86_64-pc-windows-msvc" not in version:
        raise AssertionError("MSVC reference required for native math parity")
    driver=rust_driver(args.source.resolve(),output)
    names=re.findall(r"#\[test\]\s*fn (\w+)",(args.source/"src/geom2d/tessellate.rs").read_text(encoding="utf-8"))
    assert names==(ROOT/"tests/cadkernel/tessellate2/expected.txt").read_text().splitlines() and len(names)==11
    if not args.skip_build:
        for mode in ("O0","O2"):
            options=[args.rustc,"--edition=2021","--crate-name","area_reference",driver,"-C",f"opt-level={mode[1]}","-A","dead_code","-A","unused_variables"]
            compile_program([*options,"-o",output/f"reference-{mode}.out"])
            upstream=output/f"upstream-{mode}.out"
            compile_program([*options,"--test","-o",upstream])
            _,text,_=run([upstream,"geom2d::tessellate::tests::"])
            assert "11 passed; 0 failed" in text,text
            (output/f"upstream-{mode}.txt").write_text(text,encoding="utf-8")
            print(f"Rust {mode}: 11 source tests passed",flush=True)
            for fixture in ("tessellate2", "tessellate2_arc_overflow", "tessellate2_ellipse_overflow"):
                print(COMMON["verify_fixture"](ROOT/"tests/cadkernel"/fixture,mode,args.compiler,output,60,30,False),flush=True)
            compile_program([args.compiler,ROOT/"tests/cadkernel/tessellate2/probe.dl",f"-{mode}","-o",output/f"probe-{mode}.out"])
    count=fields=0
    for name,shape in inputs():
        if args.case not in name: continue
        arguments=[str(x) for x in shape]
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
    report=dict(revision=CURVE["PIN"],compiler_sha256=digest,rustc=version,source_tests=11,cases=count,fields=fields,case_filter=args.case,exact_native_optimization_parity=True,exact_rust_optimization_parity=True)
    report["native_source_sha256"]={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [ROOT/"lib/cadkernel/tessellate2.dl",*sorted((ROOT/"tests/cadkernel/tessellate2").glob("*.dl"))]}
    (output/("filtered-summary.json" if args.case else "summary.json")).write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print(f"PASS: {count} cases, {fields} Rust comparisons; artifacts={output}",flush=True)


if __name__=="__main__":
    main()
