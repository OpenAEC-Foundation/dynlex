# SPDX-License-Identifier: MPL-2.0
"""Runtime differential for complete spatial arc/line unions and chain reduction."""
import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import random
import shutil
import sys

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
# Share the guarded subprocess, pinned-blob and exact numeric comparison helpers.
sys.path.insert(0,str(HERE.parent/"polygon3_mesh"))
from verify import invoke, run, pinned, number, same, PIN


def arc(start=0.0,end=math.pi,radius=2.0,normal=(0.0,0.0,1.0)):
    return [0.0,0.0,0.0,*normal,radius,start,end]


def cases():
    result=[]
    def add(label,mode,*values):
        result.append((label,mode,[mode,*values]))
    spans=[(0.0,math.pi),(math.pi,math.tau),(-math.pi,0.0),(-0.0,math.pi),
           (0.0,0.0),(0.0,math.tau),(0.0,2*math.tau),(1.0,2.0),(2.0,3.0),
           (5.0,1.0),(6.0,3.0),(-1.0,0.1),(0.0,-5e-324),(5e-324,1.0),
           (0.0,math.nextafter(math.tau,0.0)),(math.tau,math.tau+math.pi),
           (1e20,1e20+1e6),(1e308,-1e308)]
    for a in spans:
        shape=arc(*a)
        add(f"contains {a}",1,*shape[:7],*shape)
        for b in spans:
            for tolerance in (0.0,1e-7,10.0):
                add(f"arc {a}/{b}/{tolerance}",0,tolerance,*shape,*arc(*b))
    for field in range(9):
        for value in [math.nan,math.inf,-math.inf]:
            shape=arc();shape[field]=value
            add(f"arc nonfinite {field}/{value}",0,1e-7,*shape,*shape)
            add(f"contains nonfinite {field}/{value}",1,*shape[:7],*shape)
    for field in range(7):
        shape=arc();shape[field]+=1e-12
        add(f"arc support mismatch {field}",0,1.0,*arc(),*shape)
        add(f"contains support mismatch {field}",1,*arc()[:7],*shape)
    for normal in [(0,0,0),(0,0,5e-324),(0,0,1e-150),(0,0,1e150),
                   (0,0,1e308),(1e308,1e308,1e308),(0,0,-1)]:
        shape=arc(normal=normal)
        add(f"arc normal {normal}",0,0.0,*shape,*shape)
        add(f"contains normal {normal}",1,*shape[:7],*shape)
    for radius in [0.0,-0.0,-1.0,5e-324,1e-150,1e150,1e308]:
        shape=arc(radius=radius)
        add(f"arc radius {radius}",0,1e-7,*shape,*shape)
        add(f"contains radius {radius}",1,*shape[:7],*shape)
    for tolerance in [-0.0,-1.0,math.nan,math.inf,-math.inf,5e-324,1e308]:
        add(f"arc tolerance {tolerance}",0,tolerance,*arc(),*arc(math.pi,math.tau))
    for delta in [math.nextafter(1e-7,0.0),1e-7,math.nextafter(1e-7,math.inf),-1e-7]:
        add(f"angular tolerance endpoint {delta}",0,2e-7,*arc(0.0,1.0),*arc(1.0+delta,2.0))

    def line(label,points,tolerance=1e-7):
        add(label,2,tolerance,*(x for p in points for x in p))
    for start,end in [(-2,-1),(-1,0),(0,1),(1,2),(2,3),(3,4),(4,5),
                      (-2,5),(1,1),(1,3),(3,1)]:
        for offset in [0.0,math.nextafter(1e-7,0.0),1e-7,math.nextafter(1e-7,math.inf),1.0]:
            for reverse in (False,True):
                a=[(0,0,0),(4,0,0)]
                if reverse:a.reverse()
                line(f"line {start}/{end}/{offset}/{reverse}",[*a,(start,offset,0),(end,offset,0)])
    base=[(0.0,0.0,0.0),(4.0,0.0,0.0),(2.0,0.0,0.0),(5.0,0.0,0.0)]
    for tolerance in [0.0,-0.0,5e-324,-1.0,math.nan,math.inf,-math.inf,4.0,1e308]:
        line(f"line tolerance {tolerance}",base,tolerance)
    for field in range(12):
        for value in [math.nan,math.inf,-math.inf]:
            points=[list(p) for p in base];points[field//3][field%3]=value
            line(f"line nonfinite {field}/{value}",points)
    for scale in [5e-324,1e-200,1e-150,1e-9,1.0,1e100,1e154,1e307]:
        for tolerance in [0.0,1e-7]:
            line(f"line scale {scale}/{tolerance}",[tuple(x*scale for x in p) for p in base],tolerance)
    for delta in [math.nextafter(1e-7,0.0),1e-7,math.nextafter(1e-7,math.inf),2e-7]:
        line(f"line gap {delta}",[(0,0,0),(1e-7,0,0),(1e-7+delta,0,0),(1,0,0)],1e-7)
    line("overflow finite deltas",[(-1e308,0,0),(1e308,0,0),(0,0,0),(1,0,0)],0.0)
    line("signed zero",[(-0.0,-0.0,-0.0),(4,-0.0,-0.0),(-0.0,-0.0,-0.0),(4,-0.0,-0.0)],0.0)

    def chain(label,points,tolerance=1e-7):
        add(label,3,tolerance,len(points),*(x for p in points for x in p))
    chains=[[],[(0,0,0)],[(0,0,0),(0,0,0)],[(0,0,0),(1,0,0),(2,0,0)],
            [(0,0,0),(1,0,0),(2,0,0),(1,0,0)],[(0,0,0),(1,0,0),(2,1,0)],
            [(0,0,0),(1,0,0),(2,0,0),(3,0,0),(4,0,0)],
            [(0,0,0),(1,0,0),(1,1,0),(2,1,0),(3,1,0)],
            [(0,0,0),(1,0,0),(2,0,0),(2,0,0),(0,0,0)],
            [(-1e308,0,0),(0,0,0),(1e308,0,0)]]
    for index,points in enumerate(chains):
        for tolerance in [0.0,1e-7,1.0,2.0,math.nan,-1.0]:
            chain(f"chain {index}/{tolerance}",points,tolerance)
    for value in [math.nan,math.inf,-math.inf]:
        for axis in range(3):
            points=[list(p) for p in chains[3]];points[1][axis]=value
            chain(f"chain nonfinite {value}/{axis}",points)

    rng=random.Random(9535463)
    for index in range(160):
        first=rng.uniform(-100,100);second=rng.uniform(-100,100)
        add(f"arc random {index}",0,rng.choice([0.0,1e-7,.1]),
            *arc(first,first+rng.uniform(-10,10)),*arc(second,second+rng.uniform(-10,10)))
        origin=[rng.uniform(-1e6,1e6) for _ in range(3)] if index%2 else [0.0]*3
        axis=[rng.uniform(-2,2) for _ in range(3)]
        positions=[rng.uniform(-10,10) for _ in range(4)]
        points=[[o+t*d for o,d in zip(origin,axis)] for t in positions]
        if index%4==0:points[-1][2]+=.01
        line(f"line random {index}",points)
        points=[[o+j*d for o,d in zip(origin,axis)] for j in range(rng.randrange(0,30))]
        if points and index%3==0:points.insert(len(points)//2,points[0])
        if points and index%5==0:points.append(points[-1])
        chain(f"chain random {index}",points)
    for a in range(3):
        for b in range(3):
            for coordinate in [0.0,-0.0,1.0,math.nan,math.inf]:
                add(f"derived equality {a}/{b}/{coordinate}",4,coordinate,0,0,1,0,0,a,
                    coordinate,0,0,1,0,0,b)
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source",type=Path,required=True)
    parser.add_argument("--compiler",type=Path,default=ROOT/"build/dynlex.exe")
    installed=Path.home()/".rustup/toolchains/stable-x86_64-pc-windows-msvc/bin/rustc.exe"
    parser.add_argument("--rustc",type=Path,default=installed if installed.is_file() else shutil.which("rustc"))
    args=parser.parse_args();source=args.source.resolve()
    build=ROOT/"build/cadkernel-union3-checks";build.mkdir(parents=True,exist_ok=True)
    text="#![allow(dead_code)]\nmod space {\n"
    for module in ["vec","arc_union","line_union"]:
        path=f"src/space/{module}.rs";pinned(source,path)
        text+=f'#[path=r"{(source/path).as_posix()}"] pub mod {module};\n'
    text+='pub use vec::Vec3;\n}\n'+f'include!(r"{(HERE/"reference.rs").as_posix()}");\n'
    driver=build/"reference-driver.rs";driver.write_text(text,encoding="utf-8")
    common=[args.rustc,"--edition=2021","--crate-name","union_reference",driver]
    reference=build/"reference.out";run([*common,"-O","-o",reference],quiet=True)
    source_tests=build/"source-tests.out";run([*common,"--test","-o",source_tests],quiet=True)
    source_output=""
    for name,count in [("arc_union",2),("line_union",1)]:
        output=run([source_tests,f"space::{name}::tests::"])
        assert f"{count} passed; 0 failed" in output,output
        source_output+=output
    (build/"source-tests.txt").write_text(source_output,encoding="utf-8")
    executables=[reference]
    for level in ["O0","O2"]:
        fixture=build/f"fixture-{level}.out"
        run([args.compiler,HERE/"main.dl",f"-{level}","-o",fixture],quiet=True)
        assert run([fixture]).strip()==(HERE/"expected.txt").read_text().strip()
        probe=build/f"probe-{level}.out"
        run([args.compiler,HERE/"probe.dl",f"-{level}","-o",probe],quiet=True)
        executables.append(probe)
        refusal=invoke([args.compiler,HERE/"wrong-points.dl",f"-{level}","-o",build/"wrong.out"])
        assert refusal.returncode==1 and "cad vector3" in refusal.stderr,refusal
        (build/f"wrong-points-{level}.txt").write_text(refusal.stdout+refusal.stderr,encoding="utf-8")
        print(f"{level}: fixture passed; wrong points refused",flush=True)
    dataset=cases();comparisons=0
    for index,(label,mode,values) in enumerate(dataset,1):
        arguments=[str(v) for v in values]
        outputs=[[number(v) for v in run([program,*arguments]).splitlines()] for program in executables]
        expected=outputs[0]
        for level,actual in zip(["O0","O2"],outputs[1:]):
            if len(actual)!=len(expected) or any(not same(a,e) for a,e in zip(actual,expected)):
                (build/"failure.json").write_text(json.dumps({"label":label,"mode":mode,
                    "arguments":arguments,"level":level,"expected":expected,"actual":actual},indent=2),encoding="utf-8")
                raise AssertionError(f"case {index}: {label}, {level}; see {build/'failure.json'}")
            comparisons+=len(expected)
        if index%100==0:print(f"{index}/{len(dataset)} exact cases passed",flush=True)
    summary={"pin":PIN,"compiler_sha256":hashlib.sha256(args.compiler.read_bytes()).hexdigest(),
             "rustc":run([args.rustc,"--version"]).strip(),"source_tests":3,"fixture_runs":2,
             "type_refusals":2,"cases":len(dataset),"cases_per_mode":dict(Counter(str(c[1]) for c in dataset)),
             "rust_scalar_comparisons":comparisons,"comparison":"exact including zero signs; NaN classification"}
    (build/"results.json").write_text(json.dumps(summary,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(summary,indent=2),flush=True)


if __name__=="__main__":main()
