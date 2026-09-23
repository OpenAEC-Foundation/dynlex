# SPDX-License-Identifier: MPL-2.0
# Source: https://github.com/HakanSeven12/cadkernel/blob/953d546b68aef4b6692566a1a9b077fc5bd9fb4f/src/geom2d/triangulate.rs
"""Verify all triangulation APIs and private helpers against pinned Rust at O0/O2.

The reference compiles the unchanged upstream modules, with a probe appended
inside triangulate to access private helpers. The native helper driver likewise
appends to an unchanged copy of the library. Public cases import the actual file.
No production algorithm is replaced or selected by recognizing source text.
"""
import argparse
from collections import Counter
import itertools
import math
import os
from pathlib import Path
import random
import re
import shutil
import subprocess
import sys
import tempfile
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
FIXTURE = ROOT / "tests/required/cadkernel_triangulate2"
OWNERSHIP = ROOT / "tests/required/cadkernel_triangulate2_ownership"
REVISION = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"
sys.path.insert(0, str(ROOT / "scripts"))
from process_error_mode import unattended_child_processes


def invoke(command, source=None):
    with unattended_child_processes():
        return subprocess.run(list(map(str, command)), cwd=ROOT, input=source,
                              capture_output=True, text=True, encoding="utf-8", timeout=55)


def run(command, source=None):
    result = invoke(command, source)
    if result.returncode:
        raise AssertionError(f"{command}: exit {result.returncode}\n{result.stdout}{result.stderr}")
    return result.stdout


def pinned(upstream, name):
    text = (upstream / name).read_text(encoding="utf-8")
    blob = run(["git", "-c", f"safe.directory={upstream.as_posix()}", "-C", upstream, "show", f"{REVISION}:{name}"])
    if text != blob:
        raise AssertionError(f"{name} differs from its pinned Git blob")
    return text


def reference_source(upstream):
    common = pinned(upstream, "src/geom2d/mod.rs")
    start = common.index("#[derive(Debug, Clone, Copy, PartialEq)]\npub struct Tolerance")
    end = common.index("#[cfg(test)]", start)
    vector = pinned(upstream, "src/geom2d/vec.rs")
    triangle = pinned(upstream, "src/geom2d/triangulate.rs")
    probe = (HERE / "probe.rs").read_text(encoding="utf-8")
    return ("mod geom2d {\n" + common[start:end] + "\nmod vec {\n" + vector +
            "\n}\npub mod triangulate {\n" + triangle + "\n" + probe + "\n}\n}\n" +
            "fn main() { let values = std::env::args().skip(1).map(|v| v.parse::<f64>().unwrap()).collect::<Vec<_>>(); geom2d::triangulate::run_probe(&values); }\n")


def box(x=0.0, y=0.0, width=10.0, height=None):
    height = width if height is None else height
    return [(x, y), (x + width, y), (x + width, y + height), (x, y + height)]


def circle(x, y, radius, count):
    return [(x + radius * math.cos(math.tau * i / count),
             y + radius * math.sin(math.tau * i / count)) for i in range(count)]


def cases():
    result = []

    def add(name, mode, rings, linear=1e-7, tail=()):
        values = [mode, linear, len(rings)]
        for ring in rings:
            values.extend([len(ring), *(float(value) for point in ring for value in point)])
        values.extend(tail)
        result.append((name, mode, values))

    square = box()
    concave = [(0,0),(10,0),(10,4),(4,4),(4,10),(0,10)]
    narrow = [(0,0),(12,0),(12,8),(11,8),(11,2),(2,2),(1.8,2.1),(1.8,12),(0,12)]
    bowtie = [(0,0),(10,10),(10,0),(0,10)]
    fixed = [[], [(0,0)], [(0,0),(1,0)], square, concave, narrow, bowtie,
             [(0,0),(1,0),(2,0)], [(0,0),(2,0),(1,0),(1,1),(0,1)],
             [(-0.0,0.0),(0.0,-0.0),(1,0),(1,1),(0,1)],
             [(0,0),(10,0),(10,10),(5,5),(0,10),(5,5)],
             [(0,0),(10,0),(10,10),(0,10),(0,0),(0,0)],
             [(0,0),(5e-8,0),(10,0),(10,10),(0,10)],
             [(0,0),(0,0),(10,0),(10,10),(0,10)],
             [(0,0),(1,0),(math.nan,1)], [(math.inf,0),(1,0),(1,1)],
             [(0,0),(1,0),(1,-math.inf)],
             box(512345.678,4512345.678), box(0,0,1e-150),
             box(0,0,1e150), box(-1e308,-1e308,1e308),
             [(0,0),(1e-300,0),(0,1e-300)], circle(0,0,10,64)]
    for number, ring in enumerate(fixed):
        variants = [ring, list(reversed(ring))]
        if ring:
            variants.extend([ring[1:] + ring[:1], ring + ring[:1], ring + ring[:1] * 2])
        for variant, points in enumerate(variants):
            tag = f"fixed-{number}-{variant}"
            add(tag, 0, [points])
            add(tag, 2, [points])
            add(tag, 3, [points])
            for tolerance in (1e-7, 1e-14, 1.0):
                add(tag, 1, [points], tolerance)
            add(tag, 4, [points, box(2,2,2), [(5,5)]])
    for tolerance in (math.ulp(0.0), 1e-150, 1e-7, math.nextafter(10,0),10,math.nextafter(10,math.inf),1e308):
        add("frame-tolerance-boundary", 1, [square], tolerance)
    hole_sets = [[], [[]], [[(1,1)]], [[(1,1),(2,2)]], [box(3,3,4)],
                 [box(1,1,2),box(6,6,3)], [box(1,1,2),box(1,6,2)],
                 [box(3,3,4),box(3,3,4)], [box(11,1,2)], [box(-3,1,2)],
                 [box(9,9,2)], [box(2,0,2)], [box(6,6,4)],
                 [[(math.nan,2),(1,2),(1,3)]], [[(-0.0,2),(0.0,3),(-1,3)]],
                 [circle(3,3,.5,12),circle(6,3,.5,12)]]
    for number, holes in enumerate(hole_sets):
        for reversed_holes in (holes, list(reversed(holes))):
            add(f"holes-{number}", 0, [square, *reversed_holes])
            add(f"holes-{number}", 3, [square, *reversed_holes])
    add("aligned-narrow-holes",0,[narrow,circle(1.68,6,.08,24),circle(1.68,9,.08,24)])
    nested = [square,box(2,2,6),box(4,4,2),box(4.5,4.5,1)]
    for order in itertools.permutations(nested):
        add("nesting-order",2,order)
        add("nesting-order",3,order)
    interacting = [
        [square,bowtie,box(20,20,2)],
        [square,box(5,5,10),box(7,7,1),box(30,30,2)],
        [box(12,0,2),box(8,0,4),box(4,0,4),square,bowtie,box(30,30,2)],
        [square,box(10,0,10)], [square,box(10,10,2)], [square,square],
        [square,box(2,2,2),[(1,1),(9,9),(9,1),(1,9)],box(20,20,2)],
        [square,[(0,0),(5,5),(0,10)]], [square,[(0,0),(10,0),(5,5)]],
        [square,[(2,2),(3,3)],box(20,20,2)],
        [square,[(1,1),(1,1)],box(20,20,2)],
        [square,[(math.nan,1)],box(20,20,2)],
        [], [[],[]],
    ]
    for number, rings in enumerate(interacting):
        for order in (rings, list(reversed(rings))):
            add(f"components-{number}",2,order)
            add(f"components-{number}",3,order)
    for point in [(0,0),(10,0),(5,0),(5,5),(10+1e-13,5),(10+1e-10,5),(math.nan,0)]:
        for other in [square,box(1,1,1),box(10,1,1),[],bowtie]:
            add("boundary-containment",4,[square,other,[point]])
    segments = [[(0,0),(2,2),(0,2),(2,0)],[(0,0),(2,0),(1,0),(3,0)],
                [(0,0),(2,0),(2,0),(2,2)],[(0,0),(2,0),(2+1e-8,0),(3,0)],
                [(0,0),(2,0),(1,1e-14),(1,2)],[(0,0),(0,0),(0,0),(1,1)],
                [(0,0),(2,0),(1,-0.0),(1,1)],[(0,0),(1e150,1e150),(0,1e150),(1e150,0)],
                [(0,0),(math.inf,0),(math.nan,1),(1,1)]]
    for points in segments:
        for tolerance in (0.0,1e-14,1e-7,1.0):
            add("segment-tolerance",5,[points],tolerance)
            add("segment-reversed",5,[list(reversed(points))],tolerance)
    for expected in (-1.0,0.0,100.0,100.0+5e-8,100.0+2e-7,math.nan,math.inf):
        for faces in ([],[3,0,1,1,2,3],[0,0,1],[0,3,1],[0,1,8],[0,1,2]):
            add("mesh-validation",6,[square],expected,faces)
    for points in [[(0,0),(1e308,0),(0,1e308)],[(0,0),(1,0),(0,math.nan)]]:
        add("nonfinite-triangle",6,[points],1.0,[0,1,2])
    for points in (square,concave,bowtie,[(0,0),(1,0),(2,0)],[(0,0),(1,0),(0,1)]):
        for origin in [(5,5),(10,0),(10,10),(15,5),(5,0),(math.nan,5)]:
            add("ears-and-bridges",7,[points,[(i,0) for i in range(len(points))],[(10,10),origin],box(2,2,2)])
    rng = random.Random(953546)
    for index in range(80):
        count = rng.randrange(3,15)
        sx,sy = rng.choice([(1.0,1.0),(1e-4,1e4),(1e7,1e3)])
        angles = sorted(rng.random()*math.tau for _ in range(count))
        points = [(math.cos(a)*rng.uniform(.5,3)*sx, math.sin(a)*rng.uniform(.5,3)*sy) for a in angles]
        if index % 3 == 0:
            rng.shuffle(points)
        add("random-polygon",0,[points])
        add("random-frame",1,[points])
        add("random-rings",3,[points,box(30*sx,30*sy,sx,sy)])
        add("random-helpers",4,[points,square,[(rng.uniform(-5,5),rng.uniform(-5,5))]])
        add("random-segments",5,[[(rng.uniform(-10,10),rng.uniform(-10,10)) for _ in range(4)]],rng.choice((0.,1e-7,.5)))
    special=[-math.nan,-math.inf,-1e308,-1.0,-math.ulp(0.0),-0.0,0.0,math.ulp(0.0),1.0,1e308,math.inf,math.nan]
    for left in special:
        add("total-order",8,[[(left,right) for right in special]])
    for parents in [[],[-1],[-1,0,1,2],[-1,-1,1,0,2],[3,0,1,-1],[2,-1,1,2]]:
        add("ancestor-root",9,[[(parent,0) for parent in parents]])
    return result


def argument(value):
    if isinstance(value,float) and math.isnan(value):
        return "-nan" if math.copysign(1,value)<0 else "nan"
    return repr(value)


def scalar(value):
    if re.fullmatch(r"[+-]?nan(?:\([^)]*\))?",value,re.IGNORECASE):
        return math.nan
    return float(value)


def compare(actual, expected, context):
    got,want=actual.split(),expected.split()
    if len(got)!=len(want):
        raise AssertionError(f"{context}: output lengths {len(got)} != {len(want)}\n{actual}\n{expected}")
    # Rust min/max permit either zero sign on equality. Only the bounds/origin
    # fields produced by those operations admit this choice; input coordinates,
    # signed area, bridge points and all indices still compare exactly.
    # https://doc.rust-lang.org/std/primitive.f64.html#method.min
    mode=context[3]
    unspecified_zero_sign=set()
    if mode==1 and want[0]=="true":
        unspecified_zero_sign={1,2}
    if mode==4:
        cursor=2
        for _ in range(3):
            cursor+=1+2*int(want[cursor])
        if want[cursor]=="true":
            unspecified_zero_sign=set(range(cursor+1,cursor+5))
    for index,(a,b) in enumerate(zip(got,want)):
        if a in ("true","false") or b in ("true","false"):
            equal = a==b
        else:
            left,right=scalar(a),scalar(b)
            equal=(math.isnan(left) and math.isnan(right)) or left==right
            if left==0.0 and right==0.0 and index not in unspecified_zero_sign:
                equal = math.copysign(1,left)==math.copysign(1,right)
        if not equal:
            raise AssertionError(f"{context}: token {index}: {a} != {b}\nactual={got}\nexpected={want}")


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream",type=Path,required=True)
    parser.add_argument("--compiler",type=Path,default=ROOT/"build"/("dynlex.exe" if os.name=="nt" else "dynlex"))
    parser.add_argument("--rustc",default=shutil.which("rustc") or "rustc")
    parser.add_argument("--rust-target",default="x86_64-pc-windows-msvc" if os.name=="nt" else None)
    parser.add_argument("--mode",type=int,action="append",help="Run selected differential modes (0..9); omission runs all")
    args=parser.parse_args()
    upstream=args.upstream.resolve()
    head=run(["git","-c",f"safe.directory={upstream.as_posix()}","-C",upstream,"rev-parse","HEAD"]).strip()
    if head!=REVISION: raise AssertionError(f"Expected {REVISION}, found {head}")
    source=reference_source(upstream)
    selected=[case for case in cases() if args.mode is None or case[1] in args.mode]
    target=["--target",args.rust_target] if args.rust_target else []
    print(f"Pinned source: {head}; Rust target: {args.rust_target or 'host'}",flush=True)
    print(f"Differential inputs: {len(selected)}; modes: {dict(sorted(Counter(mode for _,mode,_ in selected).items()))}",flush=True)
    with tempfile.TemporaryDirectory(prefix=".verify-",dir=HERE) as scratch:
        directory=Path(scratch).resolve()
        assert directory.parent==HERE
        reference=directory/"reference.out"
        common=[args.rustc,"-","--crate-name","cadkernel_triangulate_reference","--edition=2021",*target,"-A","warnings","-C","opt-level=2"]
        run([*common,"-o",reference],source)
        rust_tests=directory/"rust-tests.out"
        run([*common,"--test","-o",rust_tests],source)
        report=run([rust_tests,"geom2d::triangulate::tests::","--quiet"])
        if "12 passed; 0 failed" not in report: raise AssertionError(report)
        print("Pinned Rust: 12/12 source tests passed",flush=True)
        references=[]
        for index,(name,mode,values) in enumerate(selected):
            references.append(run([reference,*map(argument,values)]))
            if (index+1)%300==0: print(f"Rust references: {index+1}/{len(selected)}",flush=True)
        helper_source=directory/"helpers.dl"
        helper_source.write_text((ROOT/"lib/cadkernel/triangulate2.dl").read_text(encoding="utf-8")+"\n"+(HERE/"helpers.dl").read_text(encoding="utf-8"),encoding="utf-8")
        for level in ("-O0","-O2"):
            fixture=directory/f"fixture{level}.out"
            started=time.perf_counter()
            run([args.compiler,FIXTURE/"main.dl",level,"-o",fixture])
            compile_time=time.perf_counter()-started
            actual=run([fixture])
            if actual!=(FIXTURE/"expected.txt").read_text(encoding="utf-8"): raise AssertionError(actual)
            print(f"{level}: translated 12 source tests + frame/nesting/component/clone/free fixture passed; compile {compile_time:.3f}s",flush=True)
            ownership=directory/f"ownership{level}.out"
            run([args.compiler,OWNERSHIP/"main.dl",level,"-o",ownership])
            if run([ownership])!=(OWNERSHIP/"expected.txt").read_text(encoding="utf-8"):
                raise AssertionError(f"{level}: ownership fixture mismatch")
            for name,pattern in (("wrong-point","check cad triangulation point"),("wrong-rings","check cad triangulation ring")):
                rejected=invoke([args.compiler,HERE/f"{name}.dl",level,"-o",directory/f"{name}.out"])
                diagnostic=rejected.stdout+rejected.stderr
                if rejected.returncode!=1 or "No overload matches call" not in diagnostic or pattern not in diagnostic:
                    raise AssertionError(f"{level}: unexpected {name} result: {rejected.returncode}\n{diagnostic}")
            print(f"{level}: managed ownership fixture and 2 invalid-type rejections passed",flush=True)
            public=directory/f"public{level}.out"
            helpers=directory/f"helpers{level}.out"
            run([args.compiler,HERE/"differential.dl",level,"-o",public])
            run([args.compiler,helper_source,level,"-o",helpers])
            for index,((name,mode,values),expected) in enumerate(zip(selected,references)):
                binary=public if mode<=3 else helpers
                actual=run([binary,*map(argument,values)])
                compare(actual,expected,(level,index,name,mode,values))
                if (index+1)%300==0: print(f"{level}: differential {index+1}/{len(selected)}",flush=True)
            print(f"{level}: {len(selected)} differential cases passed; exact values/order, NaN classes, signed zero except Rust min/max ties",flush=True)
    print("All triangulate2 checks passed.",flush=True)


if __name__=="__main__": main()
