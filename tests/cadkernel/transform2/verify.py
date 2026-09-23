# SPDX-License-Identifier: MPL-2.0
"""Verify all affine-map operations against unchanged pinned Rust source."""
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
    maps = [
        (1.,0.,0.,1.,0.,0.), (1.,0.,0.,1.,512345.678,4512345.678),
        (2.,0.,0.,2.,4.,-7.), (-2.,0.,0.,2.,1.,3.),
        (3.,0.,0.,1.,0.,0.), (1.,0.,0.,-1.,0.,0.),
        (1.,.3,1.5,2.,-4.,2.), (0.,1.,-1.,0.,0.,0.),
        (0.,0.,0.,0.,0.,0.), (1.,0.,0.,0.,0.,0.),
        (-0.,0.,0.,-0.,-0.,0.), (1e-13,0.,0.,1e-13,0.,0.),
        (1e150,0.,0.,1e150,0.,0.), (1e-150,0.,0.,1e-150,0.,0.),
        (math.inf,0.,0.,1.,0.,0.), (math.nan,0.,0.,1.,0.,0.)]
    shapes = [case(kind, knots=CURVE["clamped"](2,4), weights=[1.,.7,1.3,1.]) for kind in range(8)]
    for index, mapping in enumerate(maps):
        for kind, shape in enumerate(shapes):
            yield f"matrix-{index}-kind-{kind}", mapping, shape
    for y in (math.nextafter(1e-12,0.),1e-12,math.nextafter(1e-12,math.inf),
              1.-1e-9,1.,1.+1e-9,math.nextafter(1.+1e-9,math.inf)):
        for kind in (1,2,4):
            yield f"similarity-{y}-{kind}",(y,0.,0.,y,0.,0.),shapes[kind]
            yield f"unequal-{y}-{kind}",(1.,0.,0.,y,0.,0.),shapes[kind]
    for bulge in (-math.inf,-1.,-1e-12,-0.,0.,math.nextafter(1e-12,0.),1e-12,math.nextafter(1e-12,math.inf),1.,math.inf,math.nan):
        for mapping in (maps[0],maps[3],maps[4],maps[8]):
            yield f"bulge-{bulge}-{mapping}",mapping,case(4,vertices=[(0.,0.,bulge),(2.,0.,0.)])
    for kind in (1,2,3):
        for radius in (-3.,-0.,0.,1e-150,1e150,math.inf,math.nan):
            for mapping in (maps[0],maps[4]):
                yield f"radius-{kind}-{radius}-{mapping}",mapping,case(kind,radius=radius)
    for first,last in ((0.,0.),(0.,math.tau),(1.,-1.),(-14.,18.),(math.nan,1.),(0.,math.inf)):
        for kind in (2,3):
            for mapping in (maps[3],maps[4]):
                yield f"sweep-{kind}-{first}-{last}-{mapping}",mapping,case(kind,first=first,last=last)
    for axis in ((0.,0.),(.6,.8),(2.,3.),(-1.,0.)):
        yield f"ellipse-axis-{axis}",maps[6],case(3,axis=axis)
    for vertices in ([],[(2.,3.,0.)],[(0.,0.,0.),(0.,0.,0.)]):
        for closed in (False,True):
            yield f"short-chain-{vertices}-{closed}",maps[6],case(4,vertices=vertices,closed=closed)
    rng = random.Random(953546)
    for index in range(64):
        kind=index%8
        mapping=tuple(rng.uniform(-5,5) for _ in range(6))
        yield f"random-{index}",mapping,case(kind,start=(rng.uniform(-4,4),rng.uniform(-4,4)),
            radius=rng.uniform(.1,8),first=rng.uniform(-4,0),last=rng.uniform(1,6),
            knots=CURVE["clamped"](2,4),weights=[1.,.7,1.3,1.])

def rust_driver(source, output):
    driver=CROSS["rust_driver"](source,output)
    run(["git","-c",f"safe.directory={source.as_posix()}","-C",source,"diff","--exit-code",CURVE["PIN"],"--","src/geom2d/transform.rs"])
    content=driver.read_text(encoding="utf-8")
    content=content.replace("mod geom2d {\n","mod geom2d {\n"+f'#[path=r"{(source/"src/geom2d/transform.rs").as_posix()}"] pub mod transform;\n')
    content=content.replace("tests/cadkernel/cross2/reference.rs","tests/cadkernel/transform2/reference.rs")
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
    output=args.output or Path(tempfile.mkdtemp(prefix="transform2-",dir=ROOT/"build"))
    output.mkdir(exist_ok=True,parents=True)
    digest=hashlib.sha256(args.compiler.read_bytes()).hexdigest()
    _,version,_=run([args.rustc,"-vV"])
    if os.name=="nt" and "host: x86_64-pc-windows-msvc" not in version:
        raise AssertionError("MSVC reference is required for native math parity")
    driver=rust_driver(args.source.resolve(),output)
    names=re.findall(r"#\[test\]\s*fn (\w+)",(args.source/"src/geom2d/transform.rs").read_text(encoding="utf-8"))
    assert names==(ROOT/"tests/cadkernel/transform2/expected.txt").read_text().splitlines() and len(names)==18
    if not args.skip_build:
        for mode in ("O0","O2"):
            options=[args.rustc,"--edition=2021","--crate-name","transform_reference",driver,"-C",f"opt-level={mode[1]}","-A","dead_code","-A","unused_variables"]
            compile_program([*options,"-o",output/f"reference-{mode}.out"])
            upstream=output/f"upstream-{mode}.out"
            compile_program([*options,"--test","-o",upstream])
            _,text,_=run([upstream,"geom2d::transform::tests::"])
            assert "18 passed; 0 failed" in text,text
            (output/f"upstream-{mode}.txt").write_text(text,encoding="utf-8")
            print(f"Rust {mode}: 18 source tests passed",flush=True)
            for fixture in ("transform2", "transform2_ownership"):
                print(COMMON["verify_fixture"](ROOT/"tests/cadkernel"/fixture,mode,args.compiler,output,60,30,False),flush=True)
            compile_program([args.compiler,ROOT/"tests/cadkernel/transform2/probe.dl",f"-{mode}","-o",output/f"probe-{mode}.out"])
    count=fields=0
    for name,mapping,shape in inputs():
        if args.case not in name:
            continue
        values=[*mapping,len(shape),*shape]
        arguments=[str(x) for x in values]
        (output/"current-input.json").write_text(json.dumps(dict(name=name,values=values)),encoding="utf-8")
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
    report=dict(revision=CURVE["PIN"],compiler_sha256=digest,rustc=version,source_tests=18,cases=count,fields=fields,case_filter=args.case,exact_native_optimization_parity=True,exact_rust_optimization_parity=True)
    report["native_source_sha256"]={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [ROOT/"lib/cadkernel/transform2.dl",*sorted((ROOT/"tests/cadkernel/transform2").glob("*.dl"))]}
    (output/("filtered-summary.json" if args.case else "summary.json")).write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print(f"PASS: {count} cases, {fields} Rust comparisons; artifacts={output}",flush=True)

if __name__=="__main__":
    main()
