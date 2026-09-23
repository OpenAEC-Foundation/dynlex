# SPDX-License-Identifier: MPL-2.0
"""Verify implemented B-rep primitive builders against the complete pinned Rust crate."""
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
HERE=ROOT/"tests/cadkernel/brep_make"
CROSS=runpy.run_path(str(ROOT/"tests/cadkernel/cross2/verify.py"))
CURVE,MEASURE,COMMON=(CROSS[key] for key in ("CURVE","MEASURE","COMMON"))
run,compile_program=(CROSS[key] for key in ("run","compile_program"))


def inputs():
    for kind in range(4):
        yield f"baseline-{kind}",[kind,0.,0.,0.,2.,3.,4.]
        for value in (-math.inf,-1.,-0.,0.,5e-324,1e-300,1e-12,1e-6,1.,1e6,1e150,1e308,math.inf,math.nan):
            for field in range(1,7 if kind==0 else (5 if kind==2 else 6)):
                values=[kind,0.,0.,0.,2.,3.,4.];values[field]=value
                yield f"special-{kind}-{field}-{value}",values
        for origin in ((512345.678,4512345.678,91.5),(1e12,-1e12,1e12),(-0.,-0.,-0.)):
            yield f"translated-{kind}-{origin}",[kind,*origin,.5,.5,.5]
    import random
    rng=random.Random(73835)
    for index in range(48):
        yield f"random-{index}",[index%4,*[rng.uniform(-50,50) for _ in range(3)],*[rng.uniform(.01,100) for _ in range(3)]]
    for sides in (0,1,2,3,4,5,8,17):
        for top in (-1.,0.,1e-12,1.0001e-12,2.,4.,8.,math.inf,math.nan):
            yield f"pyramid-top-{sides}-{top}",[4,1.,2.,3.,4.,9.,top,sides]
    for value in (-math.inf,-1.,-0.,0.,1e-200,1e-12,1e150,1e308,math.inf,math.nan):
        for field in range(1,6):
            values=[4,0.,0.,0.,4.,9.,0.,4];values[field]=value
            yield f"pyramid-special-{field}-{value}",values
    for index in range(24):
        yield f"random-pyramid-{index}",[4,*[rng.uniform(-50,50) for _ in range(3)],rng.uniform(.01,20),rng.uniform(.01,20),rng.choice((0.,rng.uniform(.01,20))),rng.randrange(3,13)]
    yield "elliptical-cylinder",[5,0.,0.,0.,4.,2.,7.]
    yield "elliptical-cylinder-circular",[5,1.,2.,3.,3.,3.,5.]
    yield "elliptical-cylinder-near-circular",[5,0.,0.,0.,3.,3.+2e-13,5.]
    yield "elliptical-cylinder-survey",[5,512345.678,4512345.678,91.5,4.,2.,7.]
    yield "elliptical-frustum",[6,0.,0.,0.,4.,2.,1.5,7.]
    yield "elliptical-cone",[6,0.,0.,0.,4.,2.,0.,7.]
    yield "circular-frustum",[6,0.,0.,0.,4.,4.,1.5,7.]
    yield "circular-frustum-near-point",[6,0.,0.,0.,4.,4.,1e-13,7.]
    yield "ring-torus",[7,0.,0.,0.,10.,2.]
    yield "horn-torus",[7,1.,2.,3.,2.,2.]
    yield "spindle-torus",[7,0.,0.,0.,1.,2.]
    for kind, arity in ((5,3),(6,4),(7,2)):
        for field in range(arity):
            for value in (-math.inf,-1.,-0.,0.,math.inf,math.nan):
                dimensions=[3.,2.,4.,7.][:arity]
                dimensions[field]=value
                yield f"extended-special-{kind}-{field}-{value}",[kind,0.,0.,0.,*dimensions]
    for index in range(30):
        origin=[rng.uniform(-1e4,1e4) for _ in range(3)]
        if index%3==0:
            yield f"random-elliptical-cylinder-{index}",[5,*origin,rng.uniform(.01,20),rng.uniform(.01,20),rng.uniform(.01,20)]
        elif index%3==1:
            yield f"random-frustum-{index}",[6,*origin,rng.uniform(.01,20),rng.uniform(.01,20),rng.choice((0.,rng.uniform(.01,20))),rng.uniform(.01,20)]
        else:
            major=rng.uniform(.01,20)
            yield f"random-torus-{index}",[7,*origin,major,rng.uniform(.01,major*1.5)]


def rust_source(source):
    paths=[str(p.relative_to(source)).replace("\\","/") for p in sorted((source/"src").rglob("*.rs"))]
    _,revision,_=run(["git","-c",f"safe.directory={source.as_posix()}","-C",source,"rev-parse","HEAD"])
    assert revision.strip()==CURVE["PIN"]
    source_test_names=re.findall(r"#\[test\]\s*fn (\w+)",(source/"src/brep/make.rs").read_text(encoding="utf-8"))
    expected_test_names=(HERE/"source-tests.txt").read_text(encoding="utf-8").splitlines()
    assert source_test_names==expected_test_names and len(source_test_names)==36,"the pinned make.rs test inventory changed"
    run(["git","-c",f"safe.directory={source.as_posix()}","-C",source,"diff","--exit-code",CURVE["PIN"],"--",*paths,"Cargo.toml"])
    return paths


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source",type=Path,required=True)
    parser.add_argument("--compiler",type=Path,default=ROOT/"build/dynlex.exe")
    parser.add_argument("--rustc",type=Path,default=Path.home()/".rustup/toolchains/stable-x86_64-pc-windows-msvc/bin/rustc.exe")
    parser.add_argument("--dependencies",type=Path,default=ROOT/"build/topology-reference-deps/target/debug/deps")
    parser.add_argument("--case",default="")
    parser.add_argument("--output",type=Path)
    parser.add_argument("--skip-build",action="store_true")
    args=parser.parse_args()
    output=args.output or Path(tempfile.mkdtemp(prefix="brep_make-",dir=ROOT/"build"))
    output.mkdir(parents=True,exist_ok=True)
    digest=hashlib.sha256(args.compiler.read_bytes()).hexdigest()
    _,version,_=run([args.rustc,"-vV"])
    if os.name=="nt":assert "host: x86_64-pc-windows-msvc" in version
    source=args.source.resolve()
    paths=rust_source(source)
    deps=args.dependencies.resolve()
    externs=[]
    for name in ("spade","rustc_hash"):
        matches=list(deps.glob(f"lib{name}-*.rlib"))
        assert len(matches)==1,(name,matches)
        externs += ["--extern",f"{name}={matches[0]}"]
    if not args.skip_build:
        for mode in ("O0","O2"):
            library=output/f"libcadkernel-{mode}.rlib"
            options=[args.rustc,"--edition=2021","--crate-name=cadkernel",source/"src/lib.rs","--cfg",'feature="geom2d"',"--cfg",'feature="brep"',"-L",f"dependency={deps}",*externs,"-C",f"opt-level={mode[1]}"]
            compile_program([*options,"--crate-type=rlib","-o",library])
            compile_program([args.rustc,"--edition=2021",ROOT/"tests/cadkernel/brep_make/reference.rs","--extern",f"cadkernel={library}","-L",f"dependency={deps}","-C",f"opt-level={mode[1]}","-o",output/f"reference-{mode}.out"])
            for fixture in ("brep_make_cuboid","brep_make_round","brep_make_pyramid"):
                print(COMMON["verify_fixture"](ROOT/"tests/cadkernel"/fixture,mode,args.compiler,output,90,30,False),flush=True)
            status,text,elapsed=COMMON["run_process"]([str(args.compiler),str(ROOT/"tests/cadkernel/brep_make/probe.dl"),f"-{mode}","-o",str(output/f"probe-{mode}.out")],timeout=90,cwd=ROOT,phase="compilation")
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
    report=dict(revision=CURVE["PIN"],compiler_sha256=digest,rustc=version,full_module_verified=False,implemented_operations=["cuboid","faceted_solid","cylinder","sphere","elliptical_cylinder","cone","frustum","torus","wedge","pyramid","pyramid_frustum"],source_tests=36,native_groups=32,mesh_dependent_source_tests=["every_primitive_that_meshes_encloses_what_it_should","the_primitives_that_mesh_do_so_with_their_normals_out","a_pyramid_encloses_a_third_of_the_prism_around_it","a_sphere_meshes_from_what_bounds_it_rather_than_where"],cases=count,fields=fields,case_filter=args.case,exact_native_optimization_parity=True,exact_rust_optimization_parity=True)
    report["source_sha256"]={p:hashlib.sha256((source/p).read_bytes()).hexdigest() for p in paths}
    report["native_source_sha256"]={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [ROOT/"lib/cadkernel/brep_make.dl",ROOT/"lib/cadkernel/brep_result.dl",*sorted((ROOT/"tests/cadkernel/brep_make").glob("*.dl")),ROOT/"tests/cadkernel/brep_make/source-tests.txt",ROOT/"tests/cadkernel/brep_make_cuboid/main.dl",ROOT/"tests/cadkernel/brep_make_round/main.dl",ROOT/"tests/cadkernel/brep_make_pyramid/main.dl",ROOT/"tests/cadkernel/brep_make_extended/main.dl"]}
    (output/("filtered-summary.json" if args.case else "summary.json")).write_text(json.dumps(report,indent=2)+"\n")
    print(f"PASS: {count} cases, {fields} Rust comparisons; artifacts={output}",flush=True)


if __name__=="__main__":main()
