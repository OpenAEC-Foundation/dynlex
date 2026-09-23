# SPDX-License-Identifier: MPL-2.0
"""Verify the complete native surface intersector against the unchanged pinned crate."""
import argparse
from concurrent.futures import ThreadPoolExecutor
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
run, compile_program = (CROSS[key] for key in ("run", "compile_program"))
HERE = ROOT / "tests/cadkernel/brep_intersect"


def shape(kind, origin=(0., 0., 0.), x=(1., 0., 0.), y=(0., 1., 0.), radius=5., extra=math.pi/4):
    return [kind, *origin, *x, *y, radius, extra]


def inputs():
    special = (-math.inf, -1., -0., 0., 5e-324, 1e-300, 1e-12, 1e-9, 1., 1e150, 1e308, math.inf, math.nan)
    # All 36 ordered pairs, including every unsupported torus/NURBS pairing.
    for first in range(6):
        for second in range(6):
            for tol in (-math.inf, -1., -0., 1e-9, 1., math.inf, math.nan):
                yield f"dispatch-{first}-{second}-{tol}", [tol, *shape(first), *shape(second, origin=(0., 0., 3.), radius=3.)]
    pairs = [
        ("planes", shape(0), shape(0, origin=(0.,0.,3.), x=(1.,0.,0.), y=(0.,1.,1.))),
        ("plane-sphere", shape(0, origin=(0.,0.,3.)), shape(3)),
        ("spheres", shape(3), shape(3, origin=(6.,0.,0.))),
        ("plane-cylinder-closed", shape(0, x=(1.,0.,0.), y=(0.,1.,1.)), shape(1)),
        ("plane-cylinder-generators", shape(0, origin=(3.,0.,0.), x=(0.,1.,0.), y=(0.,0.,1.)), shape(1)),
        ("plane-cone", shape(0, origin=(0.,0.,4.)), shape(2, radius=10.)),
        ("cylinders", shape(1), shape(1, origin=(6.,0.,0.))),
        ("sphere-cylinder", shape(3), shape(1, radius=3.)),
        ("cone-cylinder", shape(2), shape(1, radius=3.)),
        ("cones", shape(2), shape(2, origin=(0.,0.,2.), radius=3., extra=.2)),
    ]
    for label, one, other in pairs:
        # Geometry extremes exercise invalid frames, negative/infinite radii,
        # non-finite origins, subnormal normals and cone tangent arithmetic.
        for side in (0, 1):
            for field in (1, 3, 4, 8, 10, 11):
                for value in special:
                    a, b = one.copy(), other.copy()
                    (a, b)[side][field] = value
                    yield f"{label}-side-{side}-field-{field}-{value}", [1e-9, *a, *b]
        for tol in special:
            yield f"{label}-tolerance-{tol}", [tol, *one, *other]
    # Threshold neighbours retain source branch ordering and both cone nappes.
    for epsilon in (-2e-9,-1e-9,-math.nextafter(1e-9,0.),-1e-12,0.,1e-12,math.nextafter(1e-9,0.),1e-9,2e-9):
        for height in (5.,10.,15.):
            yield f"cone-height-{height}-{epsilon}", [1e-9,*shape(0,origin=(0.,0.,height+epsilon)),*shape(2,radius=10.)]
        for base in (0.,4.,10.):
            yield f"sphere-touch-{base}-{epsilon}", [1e-9,*shape(3,radius=7.),*shape(3,origin=(base+epsilon,0.,0.),radius=3.)]
            yield f"cylinder-touch-{base}-{epsilon}", [1e-9,*shape(1,radius=7.),*shape(1,origin=(base+epsilon,0.,0.),radius=3.)]
        for reverse in (-1.,1.):
            for pair in ((2,1),(2,2),(3,1)):
                for tol in (0.,1e-12,1e-9,1e-3):
                    yield f"coaxial-{pair}-{epsilon}-{reverse}-{tol}", [tol,*shape(pair[0]),*shape(pair[1],origin=(epsilon,0.,2.),y=(0.,reverse,epsilon),radius=3.,extra=.2)]
    for slope in (-1e-12,0.,math.nextafter(1e-12,0.),1e-12,math.nextafter(1e-12,math.inf),2e-12,math.pi/4):
        for radius in (0.,1e-12,1e-9,3.,5.):
            for reverse in (-1.,1.):
                yield f"conic-denominator-{slope}-{radius}-{reverse}", [1e-9,*shape(2,extra=math.atan(slope)),*shape(2,origin=(0.,0.,2.),y=(0.,reverse,0.),radius=radius,extra=0.)]
    rng = random.Random(44919)
    for index in range(140):
        kind1, kind2 = rng.choice([(0,0),(0,1),(0,2),(0,3),(1,1),(1,2),(1,3),(2,2),(3,3)])
        origin = tuple(rng.uniform(-20,20) for _ in range(3))
        axes = ((1.,0.,0.),(0.,1.,0.)) if index % 2 else (tuple(rng.uniform(-2,2) for _ in range(3)),tuple(rng.uniform(-2,2) for _ in range(3)))
        other_origin = tuple(origin[i]+rng.uniform(-6,6) for i in range(3))
        yield f"random-{index}", [10.**rng.uniform(-12,-3),*shape(kind1,origin,*axes,rng.uniform(.1,10),rng.uniform(-1,1)),*shape(kind2,other_origin,*axes,rng.uniform(.1,10),rng.uniform(-1,1))]


def source_check(source):
    git = ["git", "-c", f"safe.directory={source.as_posix()}", "-C", source]
    _, revision, _ = run([*git, "rev-parse", "HEAD"])
    assert revision.strip() == CURVE["PIN"]
    paths = [str(p.relative_to(source)).replace("\\","/") for p in sorted((source/"src").rglob("*.rs"))]
    run([*git, "diff", "--exit-code", CURVE["PIN"], "--", *paths, "Cargo.toml"])
    names = re.findall(r"#\[test\]\s*fn (\w+)", (source/"src/brep/intersect.rs").read_text())
    assert len(names) == 19 and names == (HERE/"expected.txt").read_text().splitlines()
    return paths


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--compiler", type=Path, default=ROOT/"build/dynlex.exe")
    parser.add_argument("--rustc", type=Path, default=Path.home()/".rustup/toolchains/stable-x86_64-pc-windows-msvc/bin/rustc.exe")
    parser.add_argument("--dependencies", type=Path, default=ROOT/"build/topology-reference-deps/target/debug/deps")
    parser.add_argument("--libraries", type=Path, default=ROOT/"build/brep-make-six-checks")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--case", default="")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()
    output = (args.output or Path(tempfile.mkdtemp(prefix="brep-intersect-",dir=ROOT/"build"))).resolve()
    output.mkdir(parents=True,exist_ok=True)
    digest = hashlib.sha256(args.compiler.read_bytes()).hexdigest()
    _, version, _ = run([args.rustc,"-vV"])
    if os.name == "nt":
        assert "host: x86_64-pc-windows-msvc" in version
    source = args.source.resolve()
    paths = source_check(source)
    deps = args.dependencies.resolve()
    externs = []
    for name in ("spade","rustc_hash"):
        matches = list(deps.glob(f"lib{name}-*.rlib"))
        assert len(matches) == 1, (name,matches)
        externs += ["--extern",f"{name}={matches[0]}"]
    if not args.skip_build:
        for mode in ("O0","O2"):
            options = [args.rustc,"--edition=2021","--crate-name=cadkernel",source/"src/lib.rs","--cfg",'feature="geom2d"',"--cfg",'feature="brep"',"-L",f"dependency={deps}",*externs,"-C",f"opt-level={mode[1]}"]
            test_binary = output/f"upstream-{mode}.out"
            compile_program([*options,"--test","-o",test_binary])
            _, tests, _ = run([test_binary,"brep::intersect::tests::","--test-threads=1"])
            assert "19 passed; 0 failed" in tests, tests
            (output/f"upstream-{mode}.txt").write_text(tests)
            print(f"19 source groups/{mode} passed",flush=True)
            library = args.libraries.resolve()/f"libcadkernel-{mode}.rlib"
            compile_program([args.rustc,"--edition=2021",HERE/"reference.rs","--extern",f"cadkernel={library}","-L",f"dependency={deps}","-C",f"opt-level={mode[1]}","-o",output/f"reference-{mode}.out"])
            for fixture in ("brep_intersect","brep_intersect_ownership"):
                print(fixture,mode,COMMON["verify_fixture"](ROOT/"tests/cadkernel"/fixture,mode,args.compiler,output,60,30,False),flush=True)
            print("additional analytic groups",mode,COMMON["verify_fixture"](ROOT/"tests/required/cadkernel_brep_intersect_analytic",mode,args.compiler,output,60,30,False),flush=True)
            status,text,elapsed = COMMON["run_process"]([str(args.compiler),str(HERE/"probe.dl"),f"-{mode}","-o",str(output/f"probe-{mode}.out")],timeout=60,cwd=ROOT,phase="compilation")
            (output/f"probe-{mode}-compile.txt").write_text(text)
            assert status == 0 and not text.strip(), text
            print(f"probe-{mode}: compile={elapsed:.3f}s",flush=True)

    def verify(case):
        name, values = case
        arguments = [str(x) for x in values]
        raw, expected_modes, actual_modes = {}, [], []
        for mode in ("O0","O2"):
            _, reference, _ = run([output/f"reference-{mode}.out",*arguments],timeout=30)
            expected, limits = MEASURE["expected_values"](reference)
            _, text, _ = run([output/f"probe-{mode}.out",*arguments],timeout=30)
            actual = CURVE["parsed"](text)
            raw[mode] = dict(reference=reference,native=text)
            def fail(message):
                (output/f"failure-{name}.json").write_text(json.dumps(dict(name=name,arguments=arguments,outputs=raw),indent=2))
                raise AssertionError(message)
            if len(actual) != len(expected):
                fail(f"{name}/{mode}: {len(actual)} fields != {len(expected)}")
            for index,(a,b,limit) in enumerate(zip(actual,expected,limits)):
                if not CURVE["equal"](a,b,exact=limit<0,absolute=max(limit,0.)):
                    fail(f"{name}/{mode} field {index}: {a} != Rust {b}")
            expected_modes.append(expected)
            actual_modes.append(actual)
        if len(expected_modes[0]) != len(expected_modes[1]) or not all(CURVE["equal"](a,b,exact=True) for a,b in zip(*expected_modes)):
            fail(f"{name}: Rust optimization divergence")
        if not all(CURVE["equal"](a,b,exact=True) for a,b in zip(*actual_modes)):
            fail(f"{name}: native optimization divergence")
        return name, sum(map(len,expected_modes))

    selected = [(name,fields) for name,fields in inputs() if args.case in name]
    assert selected
    count = fields = 0
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for name, compared in pool.map(verify,selected):
            count += 1
            fields += compared
            if count % 100 == 0:
                print(f"{count}/{len(selected)} cases passed ({name})",flush=True)
    assert hashlib.sha256(args.compiler.read_bytes()).hexdigest() == digest
    native_files = [ROOT/"lib/cadkernel/brep_intersect.dl",*sorted(HERE.glob("*.dl")),ROOT/"tests/cadkernel/brep_intersect_ownership/main.dl"]
    report = dict(revision=CURVE["PIN"],compiler_sha256=digest,rustc=version,full_module_verified=not args.case,source_tests=19,native_groups=19,ownership_groups=2,cases=count,fields=fields,case_filter=args.case,exact_native_optimization_parity=True,exact_rust_optimization_parity=True)
    report["reference_library_sha256"] = {mode:hashlib.sha256((args.libraries/f"libcadkernel-{mode}.rlib").read_bytes()).hexdigest() for mode in ("O0","O2")}
    report["source_sha256"] = {p:hashlib.sha256((source/p).read_bytes()).hexdigest() for p in paths}
    report["native_source_sha256"] = {str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in native_files}
    (output/("filtered-summary.json" if args.case else "summary.json")).write_text(json.dumps(report,indent=2)+"\n")
    print(f"PASS: {count} cases, {fields} Rust comparisons; artifacts={output}",flush=True)


if __name__ == "__main__":
    main()
