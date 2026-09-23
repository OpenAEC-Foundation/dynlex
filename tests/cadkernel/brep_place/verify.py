# SPDX-License-Identifier: MPL-2.0
"""Verify all place.rs operations against unchanged pinned Rust on O0 and O2."""
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
CURVE, MEASURE, COMMON = (CROSS[x] for x in ('CURVE', 'MEASURE', 'COMMON'))
run, compile_program = (CROSS[x] for x in ('run', 'compile_program'))
IDENTITY = [1., 0., 0., 0., 1., 0., 0., 0., 1., 0., 0., 0.]


def case(kind=4, *, damage=0, pcurve=0, sag=.04, first=0., last=1., value=.5, place=IDENTITY):
    return [kind, damage, pcurve, sag, first, last, value, *place]


def inputs():
    maps = [IDENTITY, [-1.,0.,0.,0.,1.,0.,0.,0.,1.,0.,0.,0.],
            [0.,2.,0.,-2.,0.,0.,0.,0.,2.,3.,-4.,5.],
            [-.6,-.8,0.,-.8,.6,0.,0.,0.,1.,512345.678,4512345.678,91.5],
            [1.,0.,0.,0.,2.,0.,0.,0.,1.,0.,0.,0.],
            [1.,0.,0.,1.,0.,0.,0.,0.,1.,0.,0.,0.]]
    for kind in range(6):
        for index, place in enumerate(maps):
            for pc in (-1,0,1,2,3,4,5,6,7):
                yield f'dispatch-{kind}-{index}-{pc}', case(kind,pcurve=pc,place=place)
    for kind in (0,1,4):
        for damage in range(1,18):
            for place in maps[:3]:
                yield f'damage-{kind}-{damage}-{place[0]}-{place[1]}', case(kind,damage=damage,pcurve=4,place=place)
    values = (-math.inf,-1.,-0.,0.,5e-324,1e-300,1e-12,1e-9,1.,1e150,1e308,math.inf,math.nan)
    for value in values:
        for field in (0,1,4,8,9):
            place=IDENTITY.copy();place[field]=value
            for kind in (1,4,5):
                yield f'map-{field}-{value}-{kind}',case(kind,place=place)
        for damage in (11,12,13,14):
            yield f'geometry-{damage}-{value}',case(1,damage=damage,value=value)
        yield f'sag-{value}',case(1,sag=value)
    for deviation in (math.nextafter(1e-9,0.),1e-9,math.nextafter(1e-9,math.inf),.9e-9,1.1e-9):
        for field in (3,4):
            place=IDENTITY.copy();place[field]+=deviation
            yield f'similarity-boundary-{field}-{deviation}',case(place=place)
    for first,last in ((1.,0.),(.3,.3),(-1.,2.),(0.,math.tau),(1e308,1e308),(math.nan,1.),(0.,math.inf)):
        yield f'spline-interval-{first}-{last}',case(first=first,last=last)
    for sag in (.2,.01,.0001,math.inf,math.nan):
        yield f'spline-sag-{sag}',case(sag=sag)
    yield 'spline-depth-cap',case(6,sag=0.)
    rng=random.Random(953546396)
    for index in range(40):
        angle=rng.uniform(-math.pi,math.pi);scale=10.**rng.uniform(-2,2)
        x,y=scale*math.cos(angle),scale*math.sin(angle)
        sign=-1. if index%2 else 1.
        place=[x,y,0.,-sign*y,sign*x,0.,0.,0.,scale,*[rng.uniform(-1e4,1e4) for _ in range(3)]]
        yield f'random-similarity-{index}',case(index%5,pcurve=index%8,place=place)
    for index in range(40):
        q=[rng.uniform(-1.,1.) for _ in range(4)]
        norm=math.sqrt(sum(v*v for v in q));w,x,y,z=[v/norm for v in q]
        scale=10.**rng.uniform(-2,2)
        sign=-1. if index%2 else 1.
        axes=[1.-2.*(y*y+z*z),2.*(x*y+w*z),2.*(x*z-w*y),
              2.*(x*y-w*z),1.-2.*(x*x+z*z),2.*(y*z+w*x),
              2.*(x*z+w*y),2.*(y*z-w*x),1.-2.*(x*x+y*y)]
        axes=[v*scale*(sign if i<3 else 1.) for i,v in enumerate(axes)]
        place=[*axes,*[rng.uniform(-1e4,1e4) for _ in range(3)]]
        yield f'spatial-similarity-{index}',case(index%5,pcurve=index%8,place=place)
    for scale in (-1e150,-2.,-1e-150,-0.,0.,5e-324,1e-300,1e-150,1e-12,1.,1e12,1e150,1e154,1e308,math.inf,math.nan):
        place=[scale,0.,0.,0.,scale,0.,0.,0.,scale,0.,0.,0.]
        for kind in (0,1,4,5):
            yield f'uniform-scale-{scale}-{kind}',case(kind,first=.25,last=.25,place=place)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--compiler',type=Path,default=ROOT/'build/dynlex.exe')
    parser.add_argument('--rustc',type=Path,default=Path.home()/'.rustup/toolchains/stable-x86_64-pc-windows-msvc/bin/rustc.exe')
    parser.add_argument('--dependencies',type=Path,default=ROOT/'build/topology-reference-deps/target/debug/deps')
    parser.add_argument('--libraries',type=Path,default=ROOT/'build/brep-make-six-checks')
    parser.add_argument('--output',type=Path)
    parser.add_argument('--case',default='')
    parser.add_argument('--skip-build',action='store_true')
    parser.add_argument('--build-only',action='store_true')
    args=parser.parse_args()
    output=args.output or Path(tempfile.mkdtemp(prefix='brep-place-',dir=ROOT/'build'))
    output.mkdir(parents=True,exist_ok=True)
    digest=hashlib.sha256(args.compiler.read_bytes()).hexdigest()
    source=args.source.resolve()
    _,revision,_=run(['git','-c',f'safe.directory={source.as_posix()}','-C',source,'rev-parse','HEAD'])
    assert revision.strip()==CURVE['PIN']
    paths=[str(p.relative_to(source)).replace('\\','/') for p in sorted((source/'src').rglob('*.rs'))]
    run(['git','-c',f'safe.directory={source.as_posix()}','-C',source,'diff','--exit-code',CURVE['PIN'],'--',*paths,'Cargo.toml'])
    names=re.findall(r'#\[test\]\s*fn (\w+)',(source/'src/brep/place.rs').read_text())
    assert set(names)==set((Path(__file__).with_name('expected.txt')).read_text().splitlines()) and len(names)==7
    _,version,_=run([args.rustc,'-vV'])
    if os.name=='nt':assert 'host: x86_64-pc-windows-msvc' in version
    deps=args.dependencies.resolve()
    externs=[]
    for name in ('spade','rustc_hash'):
        matches=list(deps.glob(f'lib{name}-*.rlib'));assert len(matches)==1
        externs+=['--extern',f'{name}={matches[0]}']
    if not args.skip_build:
        for mode in ('O0','O2'):
            options=[args.rustc,'--edition=2021','--crate-name=cadkernel',source/'src/lib.rs','--cfg','feature="geom2d"','--cfg','feature="brep"','-L',f'dependency={deps}',*externs,'-C',f'opt-level={mode[1]}']
            compile_program([*options,'--test','-o',output/f'upstream-{mode}.out'])
            _,tests,_=run([output/f'upstream-{mode}.out','brep::place::tests::','--test-threads=1'])
            assert '7 passed; 0 failed' in tests,tests
            (output/f'upstream-{mode}.txt').write_text(tests)
            print(f'seven unmodified source tests/{mode} passed',flush=True)
            compile_program([args.rustc,'--edition=2021',Path(__file__).with_name('reference.rs'),'--extern',f'cadkernel={args.libraries/f"libcadkernel-{mode}.rlib"}','-L',f'dependency={deps}','-C',f'opt-level={mode[1]}','-o',output/f'reference-{mode}.out'])
            fixtures=[ROOT/'tests/required'/name for name in ('cadkernel_brep_place','cadkernel_brep_place_ownership','cadkernel_brep_place_edges')]
            for fixture in fixtures:
                print(fixture.name,mode,COMMON['verify_fixture'](fixture,mode,args.compiler,output,60,30,False),flush=True)
            compile_program([args.compiler,Path(__file__).with_name('probe.dl'),f'-{mode}','-o',output/f'probe-{mode}.out'])
    if args.build_only:return
    count=fields=0
    for name,values in inputs():
        if args.case not in name:continue
        arguments=[str(v) for v in values]
        (output/'current-input.json').write_text(json.dumps(dict(name=name,values=arguments)))
        references=[];natives=[]
        for mode in ('O0','O2'):
            _,reference,_=run([output/f'reference-{mode}.out',*arguments],timeout=30)
            expected,limits=MEASURE['expected_values'](reference)
            _,text,_=run([output/f'probe-{mode}.out',*arguments],timeout=30)
            actual=CURVE['parsed'](text)
            (output/f'current-rust-{mode}.txt').write_text(reference)
            (output/f'current-native-{mode}.txt').write_text(text)
            assert len(actual)==len(expected),f'{name}/{mode}: {len(actual)} fields != {len(expected)}'
            for index,(a,b,limit) in enumerate(zip(actual,expected,limits)):
                if not CURVE['equal'](a,b,exact=limit<0,absolute=max(limit,0.)):
                    raise AssertionError(f'{name}/{mode} field {index}: {a} != Rust {b}')
            references.append(expected);natives.append(actual)
            fields+=len(expected)
        assert len(references[0])==len(references[1]) and all(CURVE['equal'](a,b,exact=True) for a,b in zip(*references)),f'{name}: Rust optimization divergence'
        assert len(natives[0])==len(natives[1]) and all(CURVE['equal'](a,b,exact=True) for a,b in zip(*natives)),f'{name}: native optimization divergence'
        count+=1
        if count%25==0:print(f'{count} cases passed ({name})',flush=True)
    assert count and hashlib.sha256(args.compiler.read_bytes()).hexdigest()==digest
    report=dict(revision=CURVE['PIN'],compiler_sha256=digest,rustc=version,source_tests=7,native_groups=7,lifecycle_groups=3,edge_case_groups=3,cases=count,fields=fields,case_filter=args.case,exact_native_optimization_parity=True,exact_rust_optimization_parity=True)
    report['reference_library_sha256']={mode:hashlib.sha256((args.libraries/f'libcadkernel-{mode}.rlib').read_bytes()).hexdigest() for mode in ('O0','O2')}
    report['source_sha256']={p:hashlib.sha256((source/p).read_bytes()).hexdigest() for p in paths}
    report['native_source_sha256']={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [ROOT/'lib/cadkernel/brep_place.dl',*sorted(Path(__file__).parent.glob('*.dl')),ROOT/'tests/cadkernel/brep_place_ownership/main.dl']}
    (output/('filtered-summary.json' if args.case else 'summary.json')).write_text(json.dumps(report,indent=2)+'\n')
    print(f'PASS: {count} cases, {fields} Rust comparisons; artifacts={output}',flush=True)


if __name__=='__main__':main()
