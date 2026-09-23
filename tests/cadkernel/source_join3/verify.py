# SPDX-License-Identifier: MPL-2.0
"""Differential checks for all five source-directed spatial join operations."""
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


def spline(start,end,degree,weights=None,closed=False,knots=None):
    points=[(start+(end-start)*i/degree,0.,0.) for i in range(degree+1)]
    knots=knots if knots is not None else [0.]*(degree+1)+[1.]*(degree+1)
    weights=weights if weights is not None else [1.]*(degree+1)
    return [degree,len(points),len(knots),len(weights),int(closed),*[x for p in points for x in p],*knots,*weights]


def inputs():
    for source in ((0.,2.),(2.,0.),(-1e150,1e150),(0.,0.),(-0.,0.)):
        for other in ((4.,5.),(5.,4.),(-2.,1.),(0.,0.)):
            for offset in (0.,1e-10,1e-8):
                yield f"line-{source}-{other}-{offset}",[0,1e-9,source[0],0.,0.,source[1],0.,0.,other[0],offset,0.,other[1],offset,0.]
    for value in (math.inf,-math.inf,math.nan,1e308):
        yield f"line-extreme-{value}",[0,1e-9,value,0.,0.,2.,0.,0.,4.,0.,0.,5.,0.,0.]
    for tolerance in (-1.,-0.,0.,math.inf,math.nan):
        yield f"line-tolerance-{tolerance}",[0,tolerance,0.,0.,0.,2.,0.,0.,4.,0.,0.,5.,0.,0.]
    spans=((0.,1.),(1.,0.),(-0.,1.),(0.,math.tau),(0.,0.),(-20.,25.),(math.inf,1.),(math.nan,2.),(-1e308,1e308))
    for first in spans:
        for second in spans:
            yield f"span-{first}-{second}",[1,0.,*first,*second]
    for normal in ((0.,0.,1.),(0.,0.,2.),(0.,0.,-1.),(0.,0.,0.),(1e-13,0.,1.),(1e-11,0.,1.),(math.inf,0.,1.)):
        for radius in (-1.,0.,2.,2.+1e-10,2.+1e-8,math.nan):
            yield f"arc-{normal}-{radius}",[2,1e-9,0.,0.,0.,0.,0.,1.,2.,0.,1.,0.,0.,0.,*normal,radius,.8,2.]
    for degree in (0,1,2,3,8,26,27):
        for end in ((2.,1.,0.),(0.,0.,0.),(math.nan,0.,0.)):
            yield f"line-nurbs-{degree}-{end}",[3,0.,degree,0.,0.,0.,*end]
    for da,db in ((1,1),(1,3),(3,1),(2,3),(3,2)):
        for start,end in ((1.,2.),(2.,1.),(-1.,0.),(0.,-1.),(3.,4.),(1.+1e-10,2.),(-1.,1e-10)):
            for weighted in (False,True):
                weights_a=[2. if i%2==0 else .5 for i in range(da+1)] if weighted else None
                weights_b=[.3 if i%2==0 else 1.2 for i in range(db+1)] if weighted else None
                first=spline(0.,1.,da,weights_a)
                second=spline(start,end,db,weights_b)
                yield f"nurbs-{da}-{db}-{start}-{end}-{weighted}",[4,1e-9,3+len(first),*first,*second]
    first=spline(0.,1.,2)
    for name,second,tolerance in (("closed",spline(1.,2.,2,closed=True),1e-9),
        ("unclamped",spline(1.,2.,2,knots=[0.,0.,.1,.9,1.,1.]),1e-9),
        ("zero-weight",spline(1.,2.,2,weights=[1.,0.,1.]),1e-9),
        ("negative-weight",spline(1.,2.,2,weights=[1.,-1.,1.]),1e-9),
        ("invalid-tolerance",spline(1.,2.,2),-1.),
        ("nan-tolerance",spline(1.,2.,2),math.nan)):
        yield f"nurbs-{name}",[4,tolerance,3+len(first),*first,*second]


def rust_driver(source,output):
    paths=["src/space/"+n+".rs" for n in ("source_join","nurbs","spline","vec")]+["src/tessellation.rs"]
    _,revision,_=run(["git","-c",f"safe.directory={source.as_posix()}","-C",source,"rev-parse","HEAD"])
    assert revision.strip()==CURVE["PIN"]
    run(["git","-c",f"safe.directory={source.as_posix()}","-C",source,"diff","--exit-code",CURVE["PIN"],"--",*paths])
    def module(name,path):return f'#[path=r"{(source/path).as_posix()}"] pub mod {name};\n'
    content=module("tessellation","src/tessellation.rs")+"mod space {\n"
    content+="".join(module(name,f"src/space/{name}.rs") for name in ("vec","spline","nurbs","source_join"))
    content+="pub use vec::Vec3;pub use nurbs::NurbsCurve3;\n}\n"
    content+=f'include!(r"{(ROOT/"tests/cadkernel/source_join3/reference.rs").as_posix()}");\n'
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
    output=args.output or Path(tempfile.mkdtemp(prefix="source-join3-",dir=ROOT/"build"))
    output.mkdir(parents=True,exist_ok=True)
    digest=hashlib.sha256(args.compiler.read_bytes()).hexdigest()
    _,version,_=run([args.rustc,"-vV"])
    if os.name=="nt":assert "host: x86_64-pc-windows-msvc" in version
    source=args.source.resolve()
    driver,paths=rust_driver(source,output)
    names=re.findall(r"#\[test\]\s*fn (\w+)",(source/"src/space/source_join.rs").read_text())
    assert names==(ROOT/"tests/cadkernel/source_join3/expected.txt").read_text().splitlines() and len(names)==2
    if not args.skip_build:
        for mode in ("O0","O2"):
            options=[args.rustc,"--edition=2021","--crate-name","join_reference",driver,"-C",f"opt-level={mode[1]}","-A","dead_code"]
            compile_program([*options,"-o",output/f"reference-{mode}.out"])
            compile_program([*options,"--test","-o",output/f"upstream-{mode}.out"])
            _,text,_=run([output/f"upstream-{mode}.out","space::source_join::tests::"])
            assert "2 passed; 0 failed" in text,text
            (output/f"upstream-{mode}.txt").write_text(text)
            for fixture in ("source_join3", "source_join3_ownership"):
                print(COMMON["verify_fixture"](ROOT/"tests/cadkernel"/fixture,mode,args.compiler,output,60,30,False),flush=True)
            compile_program([args.compiler,ROOT/"tests/cadkernel/source_join3/probe.dl",f"-{mode}","-o",output/f"probe-{mode}.out"])
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
    report["native_source_sha256"]={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [ROOT/"lib/cadkernel/source_join3.dl",*sorted((ROOT/"tests/cadkernel/source_join3").glob("*.dl"))]}
    (output/("filtered-summary.json" if args.case else "summary.json")).write_text(json.dumps(report,indent=2)+"\n")
    print(f"PASS: {count} cases, {fields} Rust comparisons; artifacts={output}",flush=True)


if __name__=="__main__":main()
