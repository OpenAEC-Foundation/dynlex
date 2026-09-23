# SPDX-License-Identifier: MPL-2.0
"""Verify complete B-rep spatial bounds against the pinned Rust crate."""
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


def raw(name,one=(0.,0.,0.,1.,1.,1.),other=(2.,0.,0.,3.,1.,1.),point=(.5,.5,.5),padding=.1,points=((0.,0.,0.),(1.,2.,3.),(-4.,5.,-6.))):
    return name,[0,padding,*one,*other,*point,len(points),*[x for p in points for x in p]]


def inputs():
    values=(-math.inf,-1.,-0.,0.,5e-324,1e-300,1e-12,1.,1e150,1e308,math.inf,math.nan)
    for value in values:
        for index in range(1,17):
            name,fields=raw(f"box-field-{index}-{value}");fields[index]=value;yield name,fields
        for points in ((),((value,value,value),),((value,0.,0.),(1.,2.,3.)),((1.,2.,3.),(value,0.,0.))):
            yield raw(f"point-set-{value}-{points}",points=points)
    for kind in range(6):
        for variant in range(14):
            for value in ((.25,) if variant<8 else values):
                yield f"body-{kind}-{variant}-{value}",[1,kind,1.,2.,3.,2.,3.,4.,variant,value]
        for value in values:
            for field in (2,5):
                fields=[1,kind,1.,2.,3.,2.,3.,4.,0,.25];fields[field]=value
                yield f"body-input-{kind}-{field}-{value}",fields
    import random
    rng=random.Random(23806)
    for index in range(60):
        points=[tuple(rng.uniform(-1e4,1e4) for _ in range(3)) for _ in range(rng.randrange(0,12))]
        yield raw(f"random-points-{index}",points=points)


def rust_source(source):
    paths=[str(p.relative_to(source)).replace("\\","/") for p in sorted((source/"src").rglob("*.rs"))]
    _,revision,_=run(["git","-c",f"safe.directory={source.as_posix()}","-C",source,"rev-parse","HEAD"])
    assert revision.strip()==CURVE["PIN"]
    run(["git","-c",f"safe.directory={source.as_posix()}","-C",source,"diff","--exit-code",CURVE["PIN"],"--",*paths,"Cargo.toml"])
    return paths


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source",type=Path,required=True)
    parser.add_argument("--compiler",type=Path,default=ROOT/"build/dynlex.exe")
    parser.add_argument("--rustc",type=Path,default=Path.home()/".rustup/toolchains/stable-x86_64-pc-windows-msvc/bin/rustc.exe")
    parser.add_argument("--dependencies",type=Path,default=ROOT/"build/topology-reference-deps/target/debug/deps")
    parser.add_argument("--libraries",type=Path,help="Existing pinned full-crate MSVC libraries; their hashes are recorded.")
    parser.add_argument("--case",default="")
    parser.add_argument("--output",type=Path)
    parser.add_argument("--skip-build",action="store_true")
    args=parser.parse_args()
    output=args.output or Path(tempfile.mkdtemp(prefix="brep_bounds-",dir=ROOT/"build"))
    output.mkdir(parents=True,exist_ok=True)
    digest=hashlib.sha256(args.compiler.read_bytes()).hexdigest()
    _,version,_=run([args.rustc,"-vV"])
    if os.name=="nt":assert "host: x86_64-pc-windows-msvc" in version
    source=args.source.resolve()
    paths=rust_source(source)
    source_names=re.findall(r"#\[test\]\s*fn (\w+)",(source/"src/brep/bounds.rs").read_text())
    native_names=(ROOT/"tests/cadkernel/brep_bounds_surfaces/expected.txt").read_text().splitlines()
    assert len(source_names)==len(native_names)==9 and set(source_names)==set(native_names)

    deps=args.dependencies.resolve()
    externs=[]
    for name in ("spade","rustc_hash"):
        matches=list(deps.glob(f"lib{name}-*.rlib"))
        assert len(matches)==1,(name,matches)
        externs += ["--extern",f"{name}={matches[0]}"]
    if not args.skip_build:
        for mode in ("O0","O2"):
            library=(args.libraries or output)/f"libcadkernel-{mode}.rlib"
            options=[args.rustc,"--edition=2021","--crate-name=cadkernel",source/"src/lib.rs","--cfg",'feature="geom2d"',"--cfg",'feature="brep"',"-L",f"dependency={deps}",*externs,"-C",f"opt-level={mode[1]}"]
            if args.libraries is None:
                compile_program([*options,"--crate-type=rlib","-o",library])
            test_binary=output/f"upstream-{mode}.out"
            compile_program([*options,"--test","-o",test_binary])
            _,tests,_=run([test_binary,"brep::bounds::tests::","--test-threads=1"])
            assert "9 passed; 0 failed" in tests,tests
            (output/f"upstream-{mode}.txt").write_text(tests)
            print(f"nine source bounds groups/{mode} passed",flush=True)
            compile_program([args.rustc,"--edition=2021",ROOT/"tests/cadkernel/brep_bounds/reference.rs","--extern",f"cadkernel={library}","-L",f"dependency={deps}","-C",f"opt-level={mode[1]}","-o",output/f"reference-{mode}.out"])
            for fixture in ("brep_bounds_surfaces",):
                print(COMMON["verify_fixture"](ROOT/"tests/cadkernel"/fixture,mode,args.compiler,output,90,30,False),flush=True)
            status,text,elapsed=COMMON["run_process"]([str(args.compiler),str(ROOT/"tests/cadkernel/brep_bounds/probe.dl"),f"-{mode}","-o",str(output/f"probe-{mode}.out")],timeout=90,cwd=ROOT,phase="compilation")
            (output/f"probe-{mode}-compile.txt").write_text(text)
            assert status==0 and not text.strip(),text
            print(f"probe-{mode}: compile={elapsed:.3f}s",flush=True)
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
    report=dict(revision=CURVE["PIN"],compiler_sha256=digest,rustc=version,full_module_verified=True,source_tests=9,native_groups=9,cases=count,fields=fields,case_filter=args.case,exact_native_optimization_parity=True,exact_rust_optimization_parity=True)
    report["reference_library_sha256"]={mode:hashlib.sha256(((args.libraries or output)/f"libcadkernel-{mode}.rlib").read_bytes()).hexdigest() for mode in ("O0","O2")}
    report["source_sha256"]={p:hashlib.sha256((source/p).read_bytes()).hexdigest() for p in paths}
    report["native_source_sha256"]={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [ROOT/"lib/cadkernel/brep_bounds.dl",*sorted((ROOT/"tests/cadkernel/brep_bounds").glob("*.dl")),ROOT/"tests/cadkernel/brep_bounds_surfaces/main.dl"]}
    (output/("filtered-summary.json" if args.case else "summary.json")).write_text(json.dumps(report,indent=2)+"\n")
    print(f"PASS: {count} cases, {fields} Rust comparisons; artifacts={output}",flush=True)


if __name__=="__main__":main()
