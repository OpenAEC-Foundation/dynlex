#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Verify the full native planar port against all pinned source tests and runtime probes."""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import random
import re
import shutil
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
PIN = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location("cadkernel_verifier", ROOT / "tests/cadkernel/verify.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


def run(command, timeout=60):
    status, output, elapsed = runner.run_process([str(x) for x in command], timeout=timeout, cwd=ROOT)
    if status:
        raise RuntimeError(f"exit {status}: {command}\n{output}")
    return output, elapsed


XY = [0.,0.,0.,1.,0.,0.,0.,1.,0.]
UPRIGHT = [0.,0.,0.,1.,0.,0.,0.,0.,1.]


def curve(kind=0, frame=XY, start=(0.,0.), end=(2.,2.), radius=2., first=0., last=math.pi/2,
          minor=1., axis=(1.,0.), points=((0.,0.,0.),(2.,0.,0.),(0.,2.,0.)),
          closed=False, degree=2, knots=(0.,0.,0.,1.,1.,1.), weights=(1.,1.,1.)):
    return [kind,*frame,*start,*end,radius,first,last,minor,*axis,len(points),int(closed),degree,
            len(knots),len(weights),*(x for p in points for x in p),*knots,*weights]


def case(name, curves, mode=0, tolerance=1e-9, parameter=.37, target=(.6,.2,.8), density=8., angle=.2):
    return name, [mode,tolerance,parameter,*target,density,angle,len(curves),*(x for c in curves for x in c)]


def cases():
    frames = [XY,UPRIGHT,[4.,5.,6.,2.,0.,0.,1.,3.,0.],
              [1e12,-1e12,1e12,.6,.8,0.,-.8,.6,0.],
              [1.,2.,3.,1.,0.,0.,2.,0.,0.]]
    for kind in range(8):
        for i,frame in enumerate(frames):
            shape=curve(kind,frame=frame)
            yield case(f"support-{kind}-{i}",[shape])
            for parameter in (-.25,.37,1.25):
                yield case(f"query-{kind}-{i}-{parameter}",[shape],mode=1,parameter=parameter)
            if i < 3:
                yield case(f"sampling-{kind}-{i}",[shape],mode=2)
    for tol in (-math.inf,-1.,-0.,0.,math.nan,math.inf,math.nextafter(2.,0.),2.,math.nextafter(2.,3.)):
        yield case(f"tolerance-{tol}",[curve(4)],tolerance=tol)
    yield case("empty-selection",[])
    yield case("empty-polyline",[curve(4,points=[])])
    for bulge in (0.,-0.,1e-300,1.,math.nan,math.inf):
        yield case(f"bulge-support-{bulge}",[curve(4,points=[(0.,0.,bulge),(2.,0.,0.)])])
    for kind in range(8):
        shifted=[*XY];shifted[2]=1.
        yield case(f"noncoplanar-{kind}",[curve(4),curve(kind,frame=shifted)])
    for coordinate in range(9):
        for value in (math.nan,math.inf,-math.inf):
            frame=[*XY];frame[coordinate]=value
            yield case(f"nonfinite-frame-{coordinate}-{value}",[curve(1,frame=frame)])
    for scale in (1e-160,1e-150,1e-12,1.,1e12,1e150,1e160,1e308):
        points=[(0.,0.,0.),(scale,0.,0.),(0.,scale,0.)]
        yield case(f"support-scale-{scale}",[curve(4,points=points)],tolerance=1e-170)
    yield case("offset-overflow",[curve(4,points=[(-1e308,0.,0.),(1e308,0.,0.),(0.,1e308,0.)])])
    yield case("control-support-not-endpoints",[curve(5),curve(0,frame=UPRIGHT,end=(0.,1.))])
    yield case("line-storage-independent",[curve(0,end=(2.,0.)),curve(6,frame=UPRIGHT,end=(0.,1.))])
    for parameter in (-0.,0.,math.nan,math.inf,-math.inf):
        yield case(f"xy-nonfinite-parameter-{parameter}",[curve(0)],mode=1,parameter=parameter)
    for value in (math.nan,math.inf,-math.inf):
        yield case(f"xy-ignored-z-{value}",[curve(0)],mode=1,target=(1.,1.,value))
    rng=random.Random(953546)
    for trial in range(30):
        # General finite frames include skew/scale and affine support noise.
        frame=[rng.uniform(-10.,10.) for _ in range(9)]
        points=[(rng.uniform(-3.,3.),rng.uniform(-3.,3.),0.) for _ in range(3)]
        shape=curve(4,frame=frame,points=points)
        yield case(f"random-support-{trial}",[shape])
        yield case(f"random-projection-{trial}",[shape],mode=1)
    for kind in range(8):
        for index,frame in enumerate(frames[:3]):
            for distance in (-1.,-0.,.37,5.):
                yield case(f"measurement-{kind}-{index}-{distance}",[curve(kind,frame=frame)],
                           mode=3,tolerance=.01,parameter=distance)
        for tolerance in (-1.,-0.,math.nan,math.inf,.1):
            yield case(f"measurement-tolerance-{kind}-{tolerance}",[curve(kind)],mode=3,tolerance=tolerance)
    yield case("measurement-rational-nurbs",[curve(5,frame=UPRIGHT,weights=(1.,.5,2.))],mode=3,tolerance=.001)
    yield case("measurement-bulged-polyline",[curve(4,frame=UPRIGHT,points=((0.,0.,1.),(2.,0.,0.),(2.,1.,0.)))],mode=3,tolerance=.001)


def numeric(value):
    return math.nan if "nan" in value.lower() else float(value)


def exact(a,b):
    return (math.isnan(a) and math.isnan(b)) or (a==b and (a!=0 or math.copysign(1.,a)==math.copysign(1.,b)))


def input_hashes(compiler):
    files={compiler,*(HERE.rglob("*.dl"))}
    pending=[ROOT/"lib/cadkernel/planar3.dl"]
    while pending:
        path=pending.pop()
        if path in files: continue
        files.add(path)
        for imported in re.findall(r"^import (lib/cadkernel/\S+)",path.read_text(encoding="utf-8"),re.M):
            pending.append(ROOT/imported)
    fixture=ROOT/"tests/required/cadkernel_planar3/main.dl"
    if fixture.exists(): files.add(fixture)
    return {str(path.relative_to(ROOT)) if path.is_relative_to(ROOT) else str(path):
            hashlib.sha256(path.read_bytes()).hexdigest() for path in sorted(files)}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source",required=True,type=Path)
    parser.add_argument("--compiler",type=Path,default=ROOT/"build"/("dynlex.exe" if os.name=="nt" else "dynlex"))
    args=parser.parse_args()
    source,compiler=args.source.resolve(),args.compiler.resolve()
    rustc=shutil.which("rustc")
    if not rustc: raise RuntimeError("rustc required")
    git=["git","-c",f"safe.directory={source.as_posix()}","-C",source]
    revision,_=run([*git,"rev-parse","HEAD"])
    if revision.strip()!=PIN: raise AssertionError("wrong source revision")
    modules=["src/tessellation.rs","src/geom2d/mod.rs","src/space/spline.rs","src/space/vec.rs",
             "src/space/plane.rs","src/space/planar.rs",*[f"src/geom2d/{name}.rs" for name in ("curve","polyline","vec","angle","nurbs","deviation","arclength")]]
    run([*git,"diff","--exit-code","HEAD","--",*modules])
    if (source/"src/space/planar.rs").read_text(encoding="utf-8").count("#[test]")!=16:
        raise AssertionError("expected sixteen source tests")
    directory=Path(tempfile.mkdtemp(prefix="planar3-verify-",dir=ROOT/"build"))
    print(f"Artifacts: {directory}",flush=True)
    inputs=input_hashes(compiler)
    (directory/"inputs.json").write_text(json.dumps(inputs,indent=2),encoding="utf-8")
    def module(group,name):
        return f'#[path=r"{(source/f"src/{group}/{name}.rs").as_posix()}"] pub mod {name};\n'
    contents=(source/"src/geom2d/mod.rs").read_text(encoding="utf-8")
    start=contents.rfind("#[derive",0,contents.index("pub struct Ellipse"))
    end=contents.index("/// Distance below which",start)
    text=f'#[path=r"{(source/"src/tessellation.rs").as_posix()}"] pub mod tessellation;\n'
    text+="mod space {\n"+"".join(module("space",name) for name in ("spline","vec","plane","planar"))+"pub use vec::Vec3;\n}\n"
    text+="mod geom2d {\n"+"".join(module("geom2d",name) for name in ("curve","polyline","vec","angle","nurbs","deviation","arclength"))
    text+="pub use vec::Vec2; pub use curve::*; pub use nurbs::NurbsCurve;\n"+contents[start:end]+"\n}\n"
    text+=f'include!(r"{(HERE/"reference.rs").as_posix()}");\n'
    driver=directory/"reference-driver.rs";driver.write_text(text,encoding="utf-8")
    tests,reference=directory/"upstream.out",directory/"reference.out"
    for extra,output in ((["--test"],tests),([],reference)):
        diagnostics,_=run([rustc,"--edition=2021","--crate-name","planar_reference","-A","dead_code",driver,*extra,"-O","-o",output])
        (directory/f"{output.stem}-build.log").write_text(diagnostics,encoding="utf-8")
    output,_=run([tests,"space::planar::"])
    (directory/"upstream-tests.log").write_text(output,encoding="utf-8")
    if "16 passed; 0 failed" not in output: raise AssertionError(output)
    print("Pinned Rust: 16/16 original tests passed",flush=True)
    selected=["basic","helpers","nurbs_ownership","measurement","all"]
    fixtures={};binaries={}
    for level in ("O0","O2"):
        for fixture in selected:
            path=HERE/fixture
            if fixture=="all" and not path.exists():
                path=ROOT/"tests/required/cadkernel_planar3"
            try:
                result=runner.verify_fixture(path,level,compiler,directory,60,20,False)
            except Exception as error:
                (directory/f"{fixture}-{level}-failure.log").write_text(str(error),encoding="utf-8")
                raise
            fixtures[f"{fixture}-{level}"]=result
            print(f"Fixture {fixture} {level}: {result}",flush=True)
        binary=directory/f"probe-{level}.out"
        diagnostics,elapsed=run([compiler,(HERE/"probe.dl").relative_to(ROOT),f"-{level}","-o",binary])
        (directory/f"probe-{level}-build.log").write_text(diagnostics,encoding="utf-8")
        if diagnostics.strip(): raise AssertionError(diagnostics)
        binaries[level]=binary
        print(f"Probe {level}: compiled in {elapsed:.3f}s",flush=True)
    total=fields=0;evidence=[]
    for name,data in cases():
        arguments=[str(x) for x in data]
        expected,_=run([reference,*arguments],20)
        rows=[row.split() for row in expected.splitlines()]
        wanted=[numeric(row[1]) for row in rows]
        exact_fields=[row[0]=="e" for row in rows]
        outputs={}
        scale=max([1.,*(abs(x) for x in wanted if math.isfinite(x))])
        for level,binary in binaries.items():
            output,_=run([binary,*arguments],20)
            actual=[numeric(x) for x in output.splitlines()]
            if len(actual)!=len(wanted): raise AssertionError(f"{name} {level}: field count {len(actual)} != {len(wanted)}")
            for i,(got,want,strict) in enumerate(zip(actual,wanted,exact_fields)):
                if exact(got,want): continue
                # Common-plane output uses only the pinned Vec3/Plane arithmetic.
                if data[0]!=0 and not strict and math.isclose(got,want,rel_tol=2e-12,abs_tol=2e-12*scale): continue
                raise AssertionError(f"{name} {level} field {i}: {got} != {want}; arguments={arguments}")
            outputs[level]=actual;fields+=len(actual)
        if not all(exact(a,b) for a,b in zip(outputs["O0"],outputs["O2"])):
            raise AssertionError(f"{name}: O0/O2 numerical parity")
        evidence.append({"case":name,"arguments":arguments,"rust":expected});total+=1
        if total%50==0: print(f"Differential: {total} cases passed",flush=True)
    (directory/"reference-cases.json").write_text(json.dumps(evidence,indent=2),encoding="utf-8")
    if input_hashes(compiler)!=inputs:
        raise AssertionError("compiler or input source changed during verification; repeat on stable inputs")
    result={"original_rust_tests":16,"translated_tests":16,
            "cases":total,"fields":fields,"fixtures":fixtures,"compiler_sha256":hashlib.sha256(compiler.read_bytes()).hexdigest()}
    (directory/"result.json").write_text(json.dumps(result,indent=2),encoding="utf-8")
    print(f"PASS: {total} differential cases; {fields} Rust field comparisons; exact O0/O2 parity",flush=True)


if __name__=="__main__": main()
