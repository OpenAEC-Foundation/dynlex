# SPDX-License-Identifier: MPL-2.0
"""Verify clip operations against unmodified pinned Rust, including trim spans."""
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
CROSS = runpy.run_path(str(ROOT / 'tests/cadkernel/cross2/verify.py'))
CURVE, MEASURE, COMMON = (CROSS[key] for key in ('CURVE', 'MEASURE', 'COMMON'))
case, run, compile_program = (CROSS[key] for key in ('case', 'run', 'compile_program'))


def inputs():
    square = case(4, closed=True, vertices=[(0., 0., 0.), (10., 0., 0.),
                                           (10., 10., 0.), (0., 10., 0.)])
    hole = case(4, closed=True, vertices=[(3., 3., 0.), (7., 3., 0.),
                                         (7., 7., 0.), (3., 7., 0.)])
    concave = case(4, closed=True, vertices=[(0., 0., 0.), (10., 0., 0.),
        (10., 4., 0.), (4., 4., 0.), (4., 10., 0.), (0., 10., 0.)])
    shapes = [case(kind, first=0., last=math.tau if kind == 3 else 1.7,
        knots=CURVE['clamped'](2, 4), weights=[1., .7, 1.3, 1.]) for kind in range(8)]
    shapes.extend([square, case(4, vertices=[(0., 0., 1.), (2., 0., -1.), (4., 0., 0.)]),
                   case(2, first=0., last=math.tau)])
    picks = [(-1., 2.), (0., 1.), (.2, .8), (.8, .2), (.4, .4), (0., 0.), (1., 1.),
             (-0., 0.), (1e-10, .8), (math.nan, .4), (.4, math.inf), (-math.inf, .4)]
    cuts = [math.nan, -math.inf, math.inf, -2., -.0, 0., 1e-12, .25, .25000000001, .75, 1., 3.]
    for kind, shape in enumerate(shapes):
        for index, (first, second) in enumerate(picks):
            yield f'break-trim-{kind}-{index}', shape, [], first, second, first, cuts, 1e-6
    for kind in (0, 1, 2, 3, 6, 7):
        for speed in (-0., 0., 1e-13, 1., 1e150, 1e200, math.inf, math.nan):
            yield f'degenerate-{kind}-{speed}', case(kind, end=(speed, 0.), radius=speed,
                minor=speed), [], .2, .8, .5, cuts, 1e-6
    boundaries = [('empty', []), ('square', [square]), ('hole', [square, hole]),
                  ('concave', [concave]), ('circle', [case(1, start=(5., 5.), radius=3.)])]
    for name, boundary in boundaries:
        for kind in range(8):
            shape = shapes[kind].copy()
            yield f'boundary-{name}-{kind}', shape, boundary, .2, .8, .5, [.25, .75], 1e-6
        for kind in (0, 6, 7):
            for y in (-1., -1e-8, 0., 1e-8, 3., 5., 7., 10., 10.00000001, 11.):
                shape = case(kind, start=(-5., y), end=(20., y) if kind == 0 else (1., 0.))
                yield f'hatch-{name}-{kind}-{y}', shape, boundary, .2, .8, .5, [.25, .75], 1e-6
    for kind in (0, 1, 4, 5, 6, 7):
        for tolerance in (1e-10, 1e-6, .1):
            for picked in (-3., -0., .25, .250000000001, .5, .75, 1., 3., math.nan, math.inf):
                yield f'trim-{kind}-{tolerance}-{picked}', shapes[kind], [], .2, .8, picked, cuts, tolerance
    rng = random.Random(953546)
    for index in range(40):
        shape = case(index % 4, start=(rng.uniform(-4, 8), rng.uniform(-4, 8)),
                     end=(rng.uniform(-4, 8), rng.uniform(-4, 8)), radius=rng.uniform(.2, 4.),
                     first=rng.uniform(-3, 3), last=rng.uniform(3, 9))
        yield f'random-{index}', shape, [square, hole], rng.random(), rng.random(), rng.random(), [rng.random() for _ in range(8)], 1e-6


def rust_driver(source, output):
    driver = CROSS['rust_driver'](source, output)
    run(['git', '-c', f'safe.directory={source.as_posix()}', '-C', source,
         'diff', '--exit-code', CURVE['PIN'], '--', 'src/geom2d/clip.rs'])
    content = driver.read_text(encoding='utf-8')
    content = content.replace('mod geom2d {\n', 'mod geom2d {\n' +
        f'#[path=r"{(source / "src/geom2d/clip.rs").as_posix()}"] pub mod clip;\n' +
        'pub use polyline::{Polyline, PolylineVertex};\n')
    content = content.replace('tests/cadkernel/cross2/reference.rs', 'tests/cadkernel/clip2/reference.rs')
    driver.write_text(content, encoding='utf-8')
    return driver


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, default=ROOT / 'build/dynlex.exe')
    parser.add_argument('--rustc', type=Path, default=Path.home() / '.rustup/toolchains/stable-x86_64-pc-windows-msvc/bin/rustc.exe')
    parser.add_argument('--case', default='')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--skip-build', action='store_true')
    args = parser.parse_args()
    output = args.output or Path(tempfile.mkdtemp(prefix='clip2-', dir=ROOT / 'build'))
    output.mkdir(exist_ok=True, parents=True)
    digest = hashlib.sha256(args.compiler.read_bytes()).hexdigest()
    _, version, _ = run([args.rustc, '-vV'])
    if os.name == 'nt' and 'host: x86_64-pc-windows-msvc' not in version:
        raise AssertionError('MSVC reference is required for native math parity')
    driver = rust_driver(args.source.resolve(), output)
    names = re.findall(r'#\[test\]\s*fn (\w+)', (args.source / 'src/geom2d/clip.rs').read_text(encoding='utf-8'))
    assert names == (ROOT / 'tests/cadkernel/clip2/expected.txt').read_text(encoding='utf-8').splitlines() and len(names) == 14
    if not args.skip_build:
        for level in ('O0', 'O2'):
            options = [args.rustc, '--edition=2021', '--crate-name', 'clip_reference', driver,
                       '-C', f'opt-level={level[1]}', '-A', 'dead_code', '-A', 'unused_variables']
            compile_program([*options, '-o', output / f'reference-{level}.out'])
            upstream = output / f'upstream-{level}.out'
            compile_program([*options, '--test', '-o', upstream])
            _, result, _ = run([upstream, 'geom2d::clip::tests::'])
            assert '14 passed; 0 failed' in result, result
            (output / f'upstream-{level}.txt').write_text(result, encoding='utf-8')
            print(f'Rust clip {level}: 14 passed', flush=True)
            for fixture in ('clip2', 'clip2_ownership'):
                print(COMMON['verify_fixture'](ROOT / f'tests/cadkernel/{fixture}', level,
                    args.compiler, output, 60, 30, False), flush=True)
            compile_program([args.compiler, ROOT / 'tests/cadkernel/clip2/probe.dl',
                             f'-{level}', '-o', output / f'probe-{level}.out'])
    count = fields = 0
    for name, shape, boundary, first, second, picked, cuts, tolerance in inputs():
        if args.case not in name:
            continue
        values = [tolerance, first, second, picked, len(cuts), *cuts, len(boundary)]
        for record in [shape, *boundary]:
            values.extend([len(record), *record])
        arguments = [str(value) for value in values]
        (output / 'current-input.json').write_text(json.dumps({'name': name, 'values': values}), encoding='utf-8')
        _, reference, _ = run([output / 'reference-O0.out', *arguments], timeout=30)
        expected, limits = MEASURE['expected_values'](reference)
        _, reference2, _ = run([output / 'reference-O2.out', *arguments], timeout=30)
        optimized, _ = MEASURE['expected_values'](reference2)
        if len(optimized) != len(expected) or any(not CURVE['equal'](a, b, exact=True) for a, b in zip(optimized, expected)):
            raise AssertionError(f'{name}: Rust optimization divergence')
        results = []
        for level in ('O0', 'O2'):
            _, result, _ = run([output / f'probe-{level}.out', *arguments], timeout=30)
            actual = CURVE['parsed'](result)
            if len(actual) != len(expected):
                raise AssertionError(f'{name}/{level}: {len(actual)} fields, expected {len(expected)}')
            for index, (a, b, limit) in enumerate(zip(actual, expected, limits)):
                if not CURVE['equal'](a, b, exact=limit < 0, absolute=max(0., limit)):
                    raise AssertionError(f'{name}/{level} field {index}: {a} != Rust {b}')
            results.append(actual)
        if any(not CURVE['equal'](a, b, exact=True) for a, b in zip(*results)):
            raise AssertionError(f'{name}: native optimization divergence')
        count += 1
        fields += 2 * len(expected)
        if count % 25 == 0:
            print(f'{count} cases passed ({name})', flush=True)
    assert count, 'no cases selected'
    assert hashlib.sha256(args.compiler.read_bytes()).hexdigest() == digest, 'compiler changed'
    evidence = dict(revision=CURVE['PIN'], compiler_sha256=digest, rustc=version,
        source_tests=14, cases=count, fields=fields, case_filter=args.case,
        exact_native_optimization_parity=True, exact_rust_optimization_parity=True)
    report = output / ('filtered-summary.json' if args.case else 'summary.json')
    report.write_text(json.dumps(evidence, indent=2) + '\n', encoding='utf-8')
    print(f'PASS: {count} cases, {fields} Rust comparisons; artifacts={output}', flush=True)


if __name__ == '__main__':
    main()
