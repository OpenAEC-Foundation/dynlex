# SPDX-License-Identifier: MPL-2.0
"""Compare both spatial polygon mesh adapters against unchanged pinned Rust.

Exact vertex counts, ordering and binary64 values (including zero signs) are
required. The seven upstream polygon tests cover measurements, not adapters.
All subprocesses inherit unattended Windows error handling.
"""
import argparse
from collections import Counter
import hashlib
import itertools
import json
import math
from pathlib import Path
import random
import re
import shutil
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
PIN = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"
sys.path.insert(0, str(ROOT / "scripts"))
from process_error_mode import unattended_child_processes


def invoke(command):
    with unattended_child_processes():
        return subprocess.run(list(map(str, command)), cwd=ROOT, capture_output=True,
                              text=True, encoding="utf-8", timeout=55)


def run(command, quiet=False):
    result = invoke(command)
    if result.returncode or result.stderr.strip() or (quiet and result.stdout.strip()):
        raise AssertionError(f"exit {result.returncode}: {command}\n{result.stdout}{result.stderr}")
    return result.stdout


def pinned(source, name):
    text = (source / name).read_text(encoding="utf-8")
    blob = run(["git", "-c", f"safe.directory={source.as_posix()}", "-C", source,
                "show", f"{PIN}:{name}"])
    assert text == blob, f"{name} differs from pinned source"
    return text


def reference_source(source):
    common = pinned(source, "src/geom2d/mod.rs")
    start = common.index("#[derive(Debug, Clone, Copy, PartialEq)]\npub struct Tolerance")
    end = common.index("#[cfg(test)]", start)
    modules = ["src/geom2d/vec.rs", "src/geom2d/triangulate.rs",
               "src/space/vec.rs", "src/space/plane.rs", "src/space/polygon.rs"]
    for name in modules:
        pinned(source, name)

    def module(name, path):
        return f'#[path=r"{(source/path).as_posix()}"] pub mod {name};\n'

    return ("#![allow(dead_code)]\nmod geom2d {\n" + common[start:end] + "\n" +
            module("vec", modules[0]) + module("triangulate", modules[1]) +
            "pub use triangulate::{polygon as triangulate, rings as triangulate_rings};\n}\n" +
            "mod space {\n" + module("vec", modules[2]) + module("plane", modules[3]) +
            "pub use plane::Plane;\n" + module("polygon", modules[4]) + "}\n" +
            f'include!(r"{(HERE/"reference.rs").as_posix()}");\n')


def box(x=0.0, y=0.0, width=10.0, height=None):
    height = width if height is None else height
    return [(x,y), (x+width,y), (x+width,y+height), (x,y+height)]


def lift(rings, frame=0):
    transforms = [lambda x,y: (x,y,5.0), lambda x,y: (x,y,y),
                  lambda x,y: (3.0,x,y), lambda x,y: (x,4.0,-y),
                  lambda x,y: (512345.678+x,4512345.678+y,30.0+0.25*x+0.5*y),
                  lambda x,y: (-2.0+2*x-y,3.0+x+3*y,-4.0-x+2*y)]
    return [[transforms[frame](x,y) for x,y in ring] for ring in rings]


def cases():
    result = []

    def add(group, label, mode, rings, tolerance=1e-7):
        values = [mode, tolerance, len(rings)]
        for ring in rings:
            values.extend([len(ring), *(v for p in ring for v in p)])
        result.append((group, label, mode, values))

    square = box()
    concave = [(0,0),(10,0),(10,4),(4,4),(4,10),(0,10)]
    bowtie = [(0,0),(10,10),(10,0),(0,10)]
    shapes = [[],[(0,0)],[(0,0),(1,0)],[(0,0),(1,0),(2,0)], square,
              concave, bowtie, [(0,0),(4,0),(2,0),(4,4),(0,4)],
              [(0,0),(10,0),(10,10),(5,5),(0,10),(5,5)],
              [(-0.0,0.0),(0.0,-0.0),(1,0),(1,1),(0,1)]]
    for shape, ring in enumerate(shapes):
        variants = [ring,list(reversed(ring))]
        if ring:
            variants.extend([ring[1:]+ring[:1],ring+ring[:1],ring+ring[:1]*2,
                             [p for point in ring for p in [point,point]]])
        for variant, points in enumerate(variants):
            for frame in (0,1,2,5):
                for mode in (0,1):
                    add("shape",f"{shape}/{variant}/frame{frame}",mode,lift([points],frame))

    ring_sets = [[square,box(3,3,4)], [square,box(1,1,2),box(6,6,3)],
                 [square,box(1,1,2),box(1,6,2)], [square,box(20,20,2)],
                 [square,box(2,2,6),box(4,4,2),box(4.5,4.5,1)],
                 [square,box(9,9,2)], [square,box(10,0,10)],
                 [square,box(10,10,2)], [square,square],
                 [square,bowtie,box(20,20,2)], [bowtie,square,box(20,20,2)],
                 [square,[(1,1),(9,9),(9,1),(1,9)],box(20,20,2)],
                 [square,[(0,0),(5,5),(0,10)]], [[],square],
                 [[(1,1),(2,2)],square], [[(0,0),(1,0),(2,0)],square],
                 [square,[],[(1,1),(2,2)]], []]
    for shape, rings in enumerate(ring_sets):
        for frame in range(6):
            add("rings",f"{shape}/frame{frame}",1,lift(rings,frame))
            add("rings",f"{shape}/reverse/frame{frame}",1,lift([list(reversed(r)) for r in rings],frame))
    nested = [square,box(2,2,6),box(4,4,2),box(4.5,4.5,1)]
    for index, rings in enumerate(itertools.permutations(nested)):
        add("nesting-order",str(index),1,lift(rings))

    limits = [5e-324,1e-150,1e-7,math.nextafter(10.0,0.0),10.0,
              math.nextafter(10.0,math.inf),1e150,1.7976931348623157e308]
    for tolerance in limits:
        for mode in (0,1):
            add("tolerance",str(tolerance),mode,lift([square]),tolerance)
    for distance in [0.0,5e-324,5e-8,math.nextafter(1e-7,0.0),1e-7,
                     math.nextafter(1e-7,math.inf),2e-7,1.0]:
        for mode in (0,1):
            for tail in (False,True):
                ring = square+[(0,distance)] if tail else [square[0],(distance,0)]+square[1:]
                add("dedup",f"{distance}/tail{tail}",mode,lift([ring]))
            warped = [(0,0,0),(10,0,0),(10,10,distance),(0,10,0)]
            add("coplanarity",f"warped/{distance}",mode,[warped])
        parallel = [[(x,y,distance) for x,y in box(3,3,4)]]
        add("coplanarity",f"hole/{distance}",1,[[(x,y,0.0) for x,y in square],*parallel])

    for nonfinite in [math.nan,math.inf,-math.inf]:
        for axis in range(3):
            for location in range(4):
                ring = [list(p) for p in lift([square])[0]]
                ring[location][axis] = nonfinite
                for mode in (0,1):
                    add("nonfinite",f"{nonfinite}/{axis}/{location}",mode,[ring])
            point = [0.0,0.0,5.0]
            point[axis] = nonfinite
            for position in (0,1):
                rings = lift([square])
                rings.insert(position,[point])
                add("nonfinite",f"short/{nonfinite}/{axis}/{position}",1,rings)

    for scale in [5e-324,1e-200,1e-100,1e-20,1e-6,1.0,1e30,1e100,1e150,1e154,1e307]:
        for mode in (0,1):
            for tolerance in [1e-7,5e-324]:
                ring = [(x*scale,y*scale,0.0) for x,y in square]
                add("scale",f"{scale}/{tolerance}",mode,[ring],tolerance)

    rng = random.Random(953546)
    for index in range(120):
        size = rng.randrange(3,22)
        ring = [(math.cos(math.tau*i/size)*rng.uniform(6,10),
                 math.sin(math.tau*i/size)*rng.uniform(6,10)) for i in range(size)]
        if index%5 == 0:
            rng.shuffle(ring)
        rings = [ring]
        if index%3 == 0:
            rings += [box(-1,-1,2)]
        if index%7 == 0:
            rings += [box(20,20,2)]
        spatial = lift(rings,index%6)
        if index%11 == 0:
            point = list(spatial[0][-1]);point[2]+=0.01;spatial[0][-1]=point
        if index%13 == 0:
            spatial[0].append(spatial[0][0])
        add("random",str(index),1,spatial)
        add("random",str(index),0,spatial[:1])
    return result


def number(value):
    if re.fullmatch(r"[+-]?nan(?:\([a-zA-Z0-9_]+\))?",value,re.IGNORECASE):
        return math.nan
    return float(value)


def same(actual, expected):
    if math.isnan(expected):
        return math.isnan(actual)
    return actual == expected and (actual != 0.0 or
                                   math.copysign(1.0,actual) == math.copysign(1.0,expected))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source",type=Path,required=True)
    parser.add_argument("--compiler",type=Path,default=ROOT/"build/dynlex.exe")
    installed=Path.home()/".rustup/toolchains/stable-x86_64-pc-windows-msvc/bin/rustc.exe"
    parser.add_argument("--rustc",type=Path,default=installed if installed.is_file() else shutil.which("rustc"))
    args=parser.parse_args()
    build=ROOT/"build/cadkernel-polygon3-mesh-checks"
    build.mkdir(parents=True,exist_ok=True)
    driver=build/"reference-driver.rs"
    driver.write_text(reference_source(args.source.resolve()),encoding="utf-8")
    common=[args.rustc,"--edition=2021","--cfg",'feature="geom2d"',"--crate-name","polygon_mesh_reference",driver]
    reference=build/"reference.out"
    run([*common,"-O","-o",reference],quiet=True)
    source_tests=build/"source-tests.out"
    run([*common,"--test","-o",source_tests],quiet=True)
    source_output=run([source_tests,"space::polygon::tests::"])
    assert "7 passed; 0 failed" in source_output,source_output
    (build/"source-tests.txt").write_text(source_output,encoding="utf-8")
    executables=[reference]
    for level in ["O0","O2"]:
        fixture=build/f"fixture-{level}.out"
        run([args.compiler,HERE/"main.dl",f"-{level}","-o",fixture],quiet=True)
        assert run([fixture]).strip()==(HERE/"expected.txt").read_text().strip()
        probe=build/f"probe-{level}.out"
        run([args.compiler,HERE/"probe.dl",f"-{level}","-o",probe],quiet=True)
        executables.append(probe)
        for invalid in ["wrong-point.dl","wrong-rings.dl"]:
            refused=invoke([args.compiler,HERE/invalid,f"-{level}","-o",build/"invalid.out"])
            assert refused.returncode==1 and "cad vector3" in refused.stderr,(invalid,refused)
            (build/f"{invalid}-{level}.txt").write_text(refused.stdout+refused.stderr,encoding="utf-8")
        print(f"{level}: fixture passed; two incorrect list types refused",flush=True)
    comparisons=0
    dataset=cases()
    for index,(group,label,mode,values) in enumerate(dataset,1):
        arguments=[str(v) for v in values]
        outputs=[[number(v) for v in run([program,*arguments]).splitlines()] for program in executables]
        expected=outputs[0]
        assert len(expected)==1+3*int(expected[0]) and int(expected[0])%3==0
        for level,actual in zip(["O0","O2"],outputs[1:]):
            if len(actual)!=len(expected) or any(not same(a,e) for a,e in zip(actual,expected)):
                failure={"group":group,"label":label,"mode":mode,"arguments":arguments,
                         "level":level,"expected":expected,"actual":actual}
                (build/"failure.json").write_text(json.dumps(failure,indent=2),encoding="utf-8")
                raise AssertionError(f"case {index}: {group}/{label}, {level}; see {build/'failure.json'}")
            comparisons+=len(expected)
        if index%100==0:
            print(f"{index}/{len(dataset)} exact cases passed",flush=True)
    invalid_count=0
    for tolerance in [0.0,-0.0,-1.0,math.nan,math.inf,-math.inf]:
        for program in executables:
            refusal=invoke([program,"0",str(tolerance),"1","0"])
            assert refusal.returncode!=0 and "tolerance must be finite and positive" in refusal.stdout+refusal.stderr,refusal
            invalid_count+=1
    summary={"source_pin":PIN,"compiler_sha256":hashlib.sha256(args.compiler.read_bytes()).hexdigest(),
             "rustc":run([args.rustc,"--version"]).strip(),"source_polygon_tests":7,
             "adapter_source_tests":0,"fixture_runs":2,"type_refusals":4,
             "invalid_tolerance_runs":invalid_count,"cases":len(dataset),
             "cases_per_api":dict(Counter(str(c[2]) for c in dataset)),
             "case_groups":dict(Counter(c[0] for c in dataset)),
             "rust_scalar_comparisons":comparisons,"comparison":"exact including order and zero signs"}
    (build/"results.json").write_text(json.dumps(summary,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(summary,indent=2),flush=True)


if __name__=="__main__":
    main()
