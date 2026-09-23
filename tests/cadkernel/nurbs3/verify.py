# SPDX-License-Identifier: MPL-2.0
"""NURBS parity against unmodified pinned Rust sources.
Run: python -B tests/cadkernel/nurbs3/verify.py --source /path/to/cadkernel
All native children inherit the Windows unattended error mode.
"""
import argparse
import copy
import math
import os
import pathlib
import random
import re
import shutil
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts"))
from process_error_mode import unattended_child_processes
PIN = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"


def run(command, timeout=60, check=True):
    started = time.perf_counter()
    with unattended_child_processes():
        result = subprocess.run(
            [str(part) for part in command], cwd=ROOT,
            capture_output=True, text=True, timeout=timeout,
        )
    if check and result.returncode:
        raise AssertionError(
            f"exit {result.returncode}: {command}\n{result.stdout}\n{result.stderr}"
        )
    return result, time.perf_counter() - started


def build(command):
    result, elapsed = run(command)
    if result.stdout:
        print(result.stdout, end="", flush=True)
    if result.stderr:
        print(result.stderr, end="", flush=True)
    print(f"compile {pathlib.Path(command[-1]).name}: {elapsed:.3f}s", flush=True)


def clamped(degree, count):
    n = count - degree
    return [0.0] * (degree + 1) + [i / n for i in range(1, n)] + [1.0] * (degree + 1)


def curve(points, degree=2, *, ctor=0, knots=(), weights=(), closed=False,
          transform=0, amount=1, u=0.37, t=0.63, tess=0, limit=0.01,
          spacing=0, first=None, last=None, evaluate=1, target=(1.1, 0.2, 0.3)):
    return [
        ctor, degree, len(points), len(knots), len(weights), int(closed),
        transform, amount, u, t, tess, limit, spacing, int(first is not None),
        int(last is not None), evaluate,
        *(x for p in points for x in p), *knots, *weights,
        *(first if first is not None else (0., 0., 0.)),
        *(last if last is not None else (0., 0., 0.)), *target,
    ]


def surface(points, ud=2, vd=2, *, ctor=0, uk=(), vk=(), weights=(),
            uc=False, vc=False, reverse=False, u=0.37, v=0.63,
            s=0.21, t=0.82, axis=0, parameter=0.43, evaluate=True):
    data = [ctor, ud, vd, len(points), len(uk), len(vk), len(weights),
            int(uc), int(vc), int(reverse), u, v, s, t, axis, parameter, int(evaluate)]
    for row in points:
        data += [len(row), *(x for p in row for x in p)]
    for row in weights:
        data += [len(row), *row]
    return [*data, *uk, *vk]


def cases():
    p = [[0., 0., 0.], [1., 2., 1.], [3., -1., 2.], [4., 1., 3.]]
    w = [1., 3., 0.75, 2.]
    k = [0., 0., 0., 0.4, 1., 1., 1.]
    for ctor in range(4):
        for closed in (False, True):
            for transform in range(5):
                for u in (-1.25, 0., 0.4, 1., 2.25):
                    yield "curve", f"constructor-{ctor}-closed-{closed}-transform-{transform}-u-{u}", curve(
                        p, ctor=ctor, knots=k, weights=w, closed=closed,
                        transform=transform, u=u, amount=1)
    for amount in (0, 1, 2, 4, 99):
        for transform in (2, 3):
            if transform == 2 and amount == 99:
                continue
            yield "curve", f"transform-{transform}-amount-{amount}", curve(
                p, ctor=1, knots=k, weights=w, transform=transform, amount=amount)
    for degree in (1, 2, 3, 15, 16, 17, 24):
        pts = [[float(i), math.sin(i), i * 0.5] for i in range(degree + 3)]
        ws = [1. + (i % 3) * 0.4 for i in range(len(pts))]
        for ctor in (1, 2, 3):
            for closed in (False, True):
                yield "curve", f"degree-{degree}-constructor-{ctor}-closed-{closed}", curve(
                    pts, degree, ctor=ctor, weights=ws, knots=clamped(degree, len(pts)),
                    closed=closed)
    for mode in (1, 2):
        for limit in (0.1, 0.01, -1., math.nan, math.inf):
            # Negative/NaN sag becomes 1e-12 upstream and can generate 65k points.
            # Exercise the branch on a line with an exact zero sag instead.
            pts = p if limit > 0 and math.isfinite(limit) else [[0.,0.,0.],[1.,0.,0.]]
            degree = 2 if pts is p else 1
            yield "curve", f"tessellation-{mode}-limit-{limit}", curve(
                pts, degree, tess=mode, limit=limit)
    for spacing in range(3):
        for ctor in (4, 5):
            for endpoints in range(4):
                yield "curve", f"fit-{ctor}-spacing-{spacing}-endpoints-{endpoints}", curve(
                    p, ctor=ctor, spacing=spacing,
                    first=(2.,1.,0.) if endpoints & 1 else None,
                    last=(1.,-1.,0.) if endpoints & 2 else None)
            for count in (0, 1, 2, 3):
                yield "curve", f"fit-count-{count}-constructor-{ctor}-spacing-{spacing}", curve(
                    p[:count], ctor=ctor, spacing=spacing)
            repeated = [[1.,1.,1.]] * 4
            yield "curve", f"repeated-fit-{ctor}-{spacing}", curve(repeated, ctor=ctor, spacing=spacing)
    for degree in (0, 4, 5):
        for ctor in range(4):
            yield "curve", f"invalid-degree-{degree}-{ctor}", curve(p, degree, ctor=ctor, knots=k, weights=w)
    for ctor in range(4):
        for pts, label in (([], "empty"), (p[:1], "singleton")):
            yield "curve", f"invalid-{label}-{ctor}", curve(pts, ctor=ctor, weights=w, knots=k)
        for bad in (0., -1., math.inf, -math.inf, math.nan):
            ws = w.copy()
            ws[1] = bad
            yield "curve", f"weight-{bad}-{ctor}", curve(p, ctor=ctor, knots=k, weights=ws, evaluate=2)
            pts = copy.deepcopy(p)
            pts[1][2] = bad if bad != 0 else -0.
            yield "curve", f"coordinate-{bad}-{ctor}", curve(pts, ctor=ctor, knots=k, weights=w, evaluate=2)
        for ws in ([], [1.], w + [1.]):
            yield "curve", f"weight-count-{len(ws)}-{ctor}", curve(p, ctor=ctor, knots=k, weights=ws)
    for ctor in (0, 1):
        knot_sets = [
            [], [0.], k + [1.], [0.] * 7, [0.,0.,0.,1.,1.,1.,2.],
            [0.,0.,0.,0.,0.,1.,1.], [0.,0.,0.,0.4,0.3,1.,1.],
            [0.,0.,0.,math.nan,1.,1.,1.], [0.,0.,0.,0.4,1.,1.,math.inf],
        ]
        for index, ks in enumerate(knot_sets):
            yield "curve", f"knots-{index}-{ctor}", curve(
                p, ctor=ctor, knots=ks, weights=w, evaluate=0)
    for weight in (1e-300, 0.999e-15, 1e-15, 1.001e-15, 1e150, 1e300):
        yield "curve", f"homogeneous-weight-{weight}", curve(p, weights=[weight] * 4)
    for u in (math.nan, math.inf, -math.inf, -0.):
        for closed in (False, True):
            yield "curve", f"parameter-{u}-closed-{closed}", curve(
                p, weights=w, u=u, t=u, closed=closed, transform=4, evaluate=2)
    for width in (0., 0.999e-15, 1e-15, 1.001e-15):
        ks = [0., 0., 0., width, 1., 1., 1.]
        yield "curve", f"repeated-span-{width}", curve(p, knots=ks, weights=w, u=width)
    # Non-clamped deletion refuses; deletion can reduce degree or preserve it.
    for degree in (1, 2, 3):
        for index in range(5):
            yield "curve", f"remove-{degree}-{index}", curve(
                p, degree, transform=3, amount=index)
        yield "curve", f"unclamped-remove-{degree}", curve(
            p, degree, knots=list(map(float, range(5 + degree))),
            transform=3, amount=1)
    # Unequal surface degrees and genuine tensor weights, with all periodic flags.
    net = [[[float(i), float(j), (i-j)*0.3 + (2. if i==1 and j==1 else 0.)]
            for j in range(4)] for i in range(3)]
    wn = [[1. + (i+j)%3 for j in range(4)] for i in range(3)]
    for ctor in (0, 1, 2):
        for flags in range(8):
            for axis in (0, 1, 2):
                yield "surface", f"constructor-{ctor}-flags-{flags}-axis-{axis}", surface(
                    net, 2, 3, ctor=ctor, weights=wn,
                    uk=clamped(2,3), vk=clamped(3,4), uc=bool(flags&1),
                    vc=bool(flags&2), reverse=bool(flags&4), axis=axis,
                    u=-0.2, v=1.3, parameter=-0.4, s=1.2, t=-0.1)
    for ud, vd in ((1,1), (2,1), (1,3), (16,1), (1,17), (17,2)):
        pts = [[[float(i), float(j), math.sin(i+j)]
                for j in range(vd+2)] for i in range(ud+2)]
        yield "surface", f"degrees-{ud}-{vd}", surface(pts,ud,vd)
    for ctor in (0, 1, 2):
        base = dict(ctor=ctor, weights=wn, uk=clamped(2,3), vk=clamped(3,4))
        for pts, label in (([], "empty"), ([[]], "empty-row"), ([net[0]], "too-few-rows"),
                           ([net[0],net[1][:-1],net[2]], "ragged")):
            yield "surface", f"{label}-{ctor}", surface(pts,2,3,**base)
        for ud,vd in ((0,3),(2,0),(3,3),(2,4)):
            yield "surface", f"invalid-degrees-{ud}-{vd}-{ctor}", surface(net,ud,vd,**base)
        for bad in (math.nan, math.inf, -math.inf):
            pts = copy.deepcopy(net)
            pts[1][1][2] = bad
            yield "surface", f"invalid-point-{bad}-{ctor}", surface(pts,2,3,**base)
        for value in (0.,-1.,math.nan,math.inf):
            ws = copy.deepcopy(wn)
            ws[1][2] = value
            yield "surface", f"invalid-weight-{value}-{ctor}", surface(net,2,3,**dict(base, weights=ws))
        for ws in ([], [wn[0]], [wn[0],wn[1][:-1],wn[2]]):
            yield "surface", f"weight-shape-{len(ws)}-{ctor}", surface(net,2,3,**dict(base,weights=ws))
        for key in ("uk","vk"):
            for label, ks in (("short", []), ("constant", [0.]*(6 if key=="uk" else 8)),
                              ("nan", [math.nan]*(6 if key=="uk" else 8))):
                yield "surface", f"knots-{key}-{label}-{ctor}", surface(
                    net,2,3,**dict(base,**{key:ks,"evaluate":False}))
    # Seam precision scales with both coordinates and weights; flags cannot unset.
    for scale in (1.,1e12):
        for delta in (0., 4e-13, 2e-12):
            pts = [[[scale,0.,0.],[scale,1.,0.]],[[scale*(1+delta),0.,0.],[scale,1.,0.]]]
            for weight_delta in (0.,4e-13,2e-12):
                yield "surface", f"seam-{scale}-{delta}-{weight_delta}", surface(
                    pts,1,1, weights=[[1.,1.],[1.+weight_delta,1.]])
    for width in (0.,1e-17,2.220446049250313e-16,3e-16,1.):
        pts = [[[0.,0.,0.],[0.,1.,0.]],[[1.,0.,1.],[1.,1.,1.]]]
        yield "surface", f"derivative-width-{width}", surface(
            pts,1,1,uk=[0.,0.,width,width],vk=[0.,0.,1.,1.],u=0.,axis=2)
    for weight in (0.,1e-300,0.999e-15,1e-15,1.001e-15,1e300):
        yield "surface", f"homogeneous-weight-{weight}", surface(net,2,3,weights=[[weight]*4 for _ in range(3)])
    for parameter in (math.nan,math.inf,-math.inf,-0.):
        yield "surface", f"nonfinite-parameter-{parameter}", surface(
            net,2,3,u=parameter,v=parameter,s=parameter,t=parameter,parameter=parameter)
    rng = random.Random(953546)
    for index in range(50):
        degree = rng.randint(1,5)
        count = degree + rng.randint(1,6)
        pts = [[rng.uniform(-10.,10.) for _ in range(3)] for _ in range(count)]
        ws = [rng.uniform(0.1,5.) for _ in range(count)]
        yield "curve", f"random-{index}", curve(
            pts,degree,ctor=1,knots=clamped(degree,count),weights=ws,
            transform=index%3,amount=1,u=rng.uniform(-0.2,1.2),t=rng.random())
    for index in range(30):
        ud,vd = rng.randint(1,4),rng.randint(1,4)
        rows,cols = ud+2,vd+2
        pts = [[[rng.uniform(-10.,10.) for _ in range(3)] for _ in range(cols)] for _ in range(rows)]
        ws = [[rng.uniform(0.1,5.) for _ in range(cols)] for _ in range(rows)]
        yield "surface", f"random-{index}", surface(
            pts,ud,vd,weights=ws,reverse=bool(index%2),axis=index%3,
            u=rng.random(),v=rng.random(),parameter=rng.random())


def parsed(text):
    values = []
    for line in text.splitlines():
        if re.fullmatch(r"[+-]?nan(?:\([a-zA-Z0-9_]+\))?", line, re.IGNORECASE):
            values.append(math.nan)
        else:
            values.append(float(line))
    return values


def equivalent(a, b, exact=False):
    if math.isnan(b):
        return math.isnan(a)
    if a == b:
        return a != 0 or math.copysign(1.,a) == math.copysign(1.,b)
    if math.isinf(a) or math.isinf(b):
        return False
    return not exact and math.isclose(a,b,rel_tol=3e-12,abs_tol=1e-300)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=pathlib.Path, required=True)
    parser.add_argument("--compiler", type=pathlib.Path,
                        default=ROOT/"build"/("dynlex.exe" if os.name=="nt" else "dynlex"))
    parser.add_argument("--reference-only", action="store_true",
                        help="validate Rust fixtures/cases without compiling DynLex")
    args = parser.parse_args()
    source, compiler = args.source.resolve(), args.compiler.resolve()
    rustc = shutil.which("rustc")
    if not rustc:
        raise RuntimeError("rustc is required")
    git = ["git","-c",f"safe.directory={source.as_posix()}","-C",source]
    revision, _ = run([*git,"rev-parse","HEAD"])
    if revision.stdout.strip() != PIN:
        raise AssertionError(f"source revision is {revision.stdout.strip()}, expected {PIN}")
    modules = ["src/space/nurbs.rs","src/space/spline.rs","src/space/vec.rs","src/tessellation.rs"]
    run([*git,"diff","--exit-code","HEAD","--",*modules])
    test_count = (source/"src/space/nurbs.rs").read_text().count("#[test]")
    if test_count != 17:
        raise AssertionError(f"unexpected upstream test count {test_count}")
    directory = ROOT/"build/cadkernel-nurbs3-checks"
    directory.mkdir(parents=True,exist_ok=True)
    print(f"source {PIN}; {test_count} upstream NURBS tests",flush=True)
    probes = {}
    for level in (() if args.reference_only else ("O0","O2")):
        refusals = {
            "wrong_bridge_dimension": ("No overload matches call 'the cad nurbs vector of [0.0, 1.0]'", "fixed array containing 2 items"),
            "wrong_bridge_precision": ("No overload matches call 'the cad nurbs vector of narrow'", "32-bit floating-point number"),
            "wrong_fit_dimension": ("No overload matches call 'check cad spline shape prototype against", "fixed array containing 2 items"),
            "local_visibility": ("tests/cadkernel/nurbs3/local_visibility.dl:", "Error: This pattern couldn't be resolved"),
        }
        for name, fragments in refusals.items():
            rejected, _ = run([compiler,f"tests/cadkernel/nurbs3/{name}.dl",f"-{level}",
                               "-o",directory/f"{name}-{level}.out"],check=False)
            diagnostic = rejected.stdout + rejected.stderr
            if rejected.returncode != 1 or any(part not in diagnostic for part in fragments):
                raise AssertionError(f"{name}/{level}: wrong rejection ({rejected.returncode}):\n{diagnostic}")
            print(f"{name} {level}: expected rejection",flush=True)
        for fixture in ("cadkernel_nurbs3","cadkernel_nurbs3_ownership"):
            output = directory/f"{fixture}-{level}.out"
            build([compiler,f"tests/required/{fixture}/main.dl",f"-{level}","-o",output])
            result, _ = run([output])
            expected = (ROOT/f"tests/required/{fixture}/expected.txt").read_text()
            if result.stdout != expected:
                raise AssertionError(f"{fixture} {level} output mismatch:\n{result.stdout}")
            print(f"{fixture} {level}: passed",flush=True)
        for kind in ("curve","surface"):
            output = directory/f"{kind}-{level}.out"
            build([compiler,f"tests/cadkernel/nurbs3/{kind}_probe.dl",f"-{level}","-o",output])
            probes[kind,level] = output
    driver = directory/"reference-driver.rs"
    def module(name, path):
        return f'#[path = r"{path.as_posix()}"] pub mod {name};\n'
    text = module("tessellation",source/modules[3])
    text += "mod space {\n"
    text += module("spline",source/modules[1])
    text += module("vector",source/modules[2])
    text += "pub use vector::Vec3;\n"
    text += module("nurbs",source/modules[0]) + "}\n"
    text += f'include!(r"{(ROOT/"tests/cadkernel/nurbs3/reference.rs").as_posix()}");\n'
    driver.write_text(text,encoding="utf-8")
    reference = directory/"reference.out"
    build([rustc,"--edition=2021","--crate-name","nurbs3_reference",driver,"-O","-o",reference])
    upstream = directory/"upstream-tests.out"
    build([rustc,"--edition=2021","--crate-name","nurbs3_tests","--test",driver,"-O","-o",upstream])
    tests, _ = run([upstream,"space::nurbs::tests::"])
    if f"{test_count} passed" not in tests.stdout:
        raise AssertionError(tests.stdout)
    print(tests.stdout,end="",flush=True)
    total = fields = 0
    for kind,name,data in cases():
        command_args = [str(x) for x in data]
        expected, _ = run([reference,kind,*command_args],timeout=30)
        want = parsed(expected.stdout)
        if args.reference_only:
            total += 1
            fields += len(want)
            if total%100 == 0:
                print(f"Rust reference: {total} cases checked",flush=True)
            continue
        results = []
        for level in ("O0","O2"):
            got, _ = run([probes[kind,level],*command_args],timeout=30)
            actual = parsed(got.stdout)
            if len(actual) != len(want):
                raise AssertionError(f"{kind}/{name}/{level}: {len(actual)} fields != {len(want)}; input={data}")
            for i,(a,b) in enumerate(zip(actual,want)):
                if not equivalent(a,b):
                    raise AssertionError(f"{kind}/{name}/{level} field {i}: {a!r} != Rust {b!r}; input={data}")
            results.append(actual)
        for i,(a,b) in enumerate(zip(*results)):
            if not equivalent(a,b,exact=True):
                raise AssertionError(f"{kind}/{name} field {i}: O0 {a!r} != O2 {b!r}")
        total += 1
        fields += len(want)*2
        if total%100 == 0:
            print(f"differential: {total} cases passed",flush=True)
    if args.reference_only:
        print(f"REFERENCE ONLY: {total} cases, {fields} output fields; DynLex not checked",flush=True)
    else:
        print(f"PASS: {total} differential cases, {fields} Rust comparisons; exact O0/O2 parity",flush=True)


if __name__ == "__main__":
    with unattended_child_processes():
        main()
