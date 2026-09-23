# SPDX-License-Identifier: MPL-2.0
"""Compare crossing and containment operations with unchanged pinned Rust."""
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
CURVE = runpy.run_path(str(ROOT / 'tests/cadkernel/curve2/verify.py'))
MEASURE = runpy.run_path(str(ROOT / 'tests/cadkernel/arclength2/verify.py'))
COMMON = runpy.run_path(str(ROOT / 'tests/cadkernel/verify.py'))
case = CURVE['case']
run = MEASURE['run']
compile_program = MEASURE['compile_program']


def inputs():
    square = case(4, closed=True, vertices=[(0., 0., 0.), (10., 0., 0.),
                                           (10., 6., 0.), (0., 6., 0.)])
    concave = case(4, closed=True, vertices=[(0., 0., 0.), (10., 0., 0.),
        (10., 4., 0.), (4., 4., 0.), (4., 10., 0.), (0., 10., 0.)])
    shapes = [case(kind, first=0., last=math.tau if kind == 3 else 1.7,
                   knots=CURVE['clamped'](2, 4), weights=[1., .7, 1.3, 1.])
              for kind in range(8)]
    for i, first in enumerate(shapes):
        for j, second in enumerate(shapes):
            # Coincident curved pairs can legitimately require exhaustive
            # subdivision; use distinct translations for the dispatch matrix.
            other = second.copy()
            other[12] += .6
            other[13] += .4
            if j in (4, 5):
                for vertex in range(int(other[7])):
                    other[22 + 3*vertex] += .6
                    other[23 + 3*vertex] += .4
            yield f'pair-{i}-{j}', [first, other], (.37, -.8), 1e-6
    for name, boundary in [('empty', []), ('square', [square]), ('concave', [concave]),
                           ('circle', [shapes[1]]), ('duplicate-square', [square, square]),
                           ('nested-circles', [case(1, radius=5.), case(1, radius=2.)])]:
        for i, point in enumerate([(0., 0.), (1., 1.), (5., 3.), (8., 8.), (-1., 3.),
                                  (5., 0.), (10., 6.), (5., 6.+1e-8)]):
            # Duplicate closed polylines are solved as analytic segments.
            yield f'boundary-{name}-{i}', boundary, point, 1e-6
    for kind in (0, 6, 7):
        for length in (0., 1e-13, 1., 1e6):
            for point in ((-10., 3.), (0., 0.), (20., -3.)):
                yield f'extent-{kind}-{length}-{point}', [case(kind, end=(length, 0.))], point, 1e-6
    for radius in (-2., -0., 0., 1e-12, 2., 1e6):
        for kind in (1, 2):
            yield f'round-radius-{kind}-{radius}', [case(kind, radius=radius)], (3., -2.), 1e-6
    for tol in (1e-3, 1e-6, 1e-10):
        for y in (0., 1.-1e-10, 1., 1.+1e-10, 2.):
            yield f'tangent-{tol}-{y}', [case(0, start=(-10., y), end=(10., y)),
                                       case(1, radius=1.)], (0., 0.), tol
    yield 'nearest-first-tie', [case(0, start=(0., -1.), end=(10., -1.)),
                               case(0, start=(0., 1.), end=(10., 1.))], (5., 0.), 1e-6
    for kind in (0, 1, 2, 6, 7):
        for value in (math.nan, math.inf, -math.inf):
            yield f'nonfinite-target-{kind}-{value}', [case(kind)], (value, 0.), 1e-6
            yield f'nonfinite-origin-{kind}-{value}', [case(kind, start=(value, 0.))], (1., 0.), 1e-6
    for bulge in (-2., -1., -.4, 0., .4, 1., 2.):
        poly = case(4, vertices=[(0., 0., bulge), (2., 0., 0.)])
        yield f'bulge-{bulge}', [poly, case(7, start=(1., -2.), end=(0., 1.))], (1., .3), 1e-6
    rng = random.Random(953546)
    for i in range(80):
        first = case(i % 8, start=(rng.uniform(-3, 3), rng.uniform(-3, 3)),
                     end=(rng.uniform(-3, 3), rng.uniform(-3, 3)),
                     radius=rng.uniform(.3, 3), minor=rng.uniform(.3, 3), first=0., last=math.tau,
                     knots=CURVE['clamped'](2, 4), weights=[1., .7, 1.3, 1.])
        second = case((i//8) % 4, start=(rng.uniform(-3, 3), rng.uniform(-3, 3)),
                      end=(rng.uniform(-3, 3), rng.uniform(-3, 3)), first=0., last=math.tau)
        yield f'random-{i}', [first, second], (rng.uniform(-4, 4), rng.uniform(-4, 4)), 1e-6


def rust_driver(source, output):
    driver = CURVE['rust_driver'](source, output)
    names = ('cross', 'containment', 'intersect')
    paths = [f'src/geom2d/{name}.rs' for name in names]
    run(['git', '-c', f'safe.directory={source.as_posix()}', '-C', source,
         'diff', '--exit-code', CURVE['PIN'], '--', *paths])
    common = (source / 'src/geom2d/mod.rs').read_text(encoding='utf-8')
    start = common.index('#[derive(Debug, Clone, Copy, PartialEq)]\npub struct Tolerance')
    end = common.index('#[cfg(test)]', start)
    additions = common[start:end] + '\n' + ''.join(
        f'#[path=r"{(source / path).as_posix()}"] pub mod {name};\n'
        for name, path in zip(names, paths))
    text = driver.read_text(encoding='utf-8').replace('mod geom2d {\n', 'mod geom2d {\n' + additions)
    text = text.replace('tests/cadkernel/curve2/reference.rs', 'tests/cadkernel/cross2/reference.rs')
    driver.write_text(text, encoding='utf-8')
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
    output = args.output or Path(tempfile.mkdtemp(prefix='cross-containment-', dir=ROOT / 'build'))
    output.mkdir(exist_ok=True, parents=True)
    digest = hashlib.sha256(args.compiler.read_bytes()).hexdigest()
    _, version, _ = run([args.rustc, '-vV'])
    if os.name == 'nt' and 'host: x86_64-pc-windows-msvc' not in version:
        raise AssertionError('MSVC reference is required for native math parity')
    source = args.source.resolve()
    driver = rust_driver(source, output)
    source_tests = {}
    for name, expected_count in [('cross', 23), ('containment', 16)]:
        names = re.findall(r'#\[test\]\s*fn (\w+)', (source / f'src/geom2d/{name}.rs').read_text(encoding='utf-8'))
        if names != (ROOT / f'tests/cadkernel/{name}2/expected.txt').read_text(encoding='utf-8').splitlines() or len(names) != expected_count:
            raise AssertionError(f'{name}: incorrect source test mapping')
        source_tests[name] = expected_count
    if not args.skip_build:
        for level in ('O0', 'O2'):
            options = [args.rustc, '--edition=2021', '--crate-name', 'crossing_reference', driver,
                       '-C', f'opt-level={level[1]}', '-A', 'dead_code', '-A', 'unused_variables']
            compile_program([*options, '-o', output / f'reference-{level}.out'])
            upstream = output / f'upstream-{level}.out'
            compile_program([*options, '--test', '-o', upstream])
            for name, count in source_tests.items():
                _, result, _ = run([upstream, f'geom2d::{name}::tests::'])
                if f'{count} passed; 0 failed' not in result:
                    raise AssertionError(result)
                (output / f'{name}-{level}.txt').write_text(result, encoding='utf-8')
                print(f'Rust {name} {level}: {count} passed', flush=True)
                print(COMMON['verify_fixture'](ROOT / f'tests/cadkernel/{name}2', level,
                    args.compiler, output, 60, 30, False), flush=True)
            print(COMMON['verify_fixture'](ROOT / 'tests/cadkernel/cross2_ownership', level,
                args.compiler, output, 60, 30, False), flush=True)
            compile_program([args.compiler, ROOT / 'tests/cadkernel/cross2/probe.dl',
                             f'-{level}', '-o', output / f'probe-{level}.out'])
    count = fields = 0
    for name, shapes, target, tolerance in inputs():
        if args.case not in name:
            continue
        values = [tolerance, *target, len(shapes)]
        for shape in shapes:
            values.extend([len(shape), *shape])
        arguments = [str(x) for x in values]
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
                    source_tests=source_tests, cases=count, fields=fields, case_filter=args.case,
                    exact_native_optimization_parity=True, exact_rust_optimization_parity=True)
    report = output / ('filtered-summary.json' if args.case else 'summary.json')
    report.write_text(json.dumps(evidence, indent=2) + '\n', encoding='utf-8')
    print(f'PASS: {count} cases, {fields} Rust comparisons; artifacts={output}', flush=True)


if __name__ == '__main__':
    main()
