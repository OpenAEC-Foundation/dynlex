# SPDX-License-Identifier: MPL-2.0
"""Verify variable-width polyline bands and source stations against pinned Rust."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import random
import re
import runpy
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[3]
ARR=runpy.run_path(str(ROOT/'tests/cadkernel/arrangement2/verify.py'))
CURVE,COMMON,run,compile_program=(ARR[key] for key in ('CURVE','COMMON','run','compile_program'))


def case(name,vertices=((0.,0.,0.),(10.,0.,0.)),closed=False,widths=None,angle=.2):
    if widths is None:widths=[(2.,2.)]*(len(vertices) if closed else max(0,len(vertices)-1))
    return name,[int(closed),angle,len(vertices),len(widths),*[x for v in vertices for x in v],*[x for w in widths for x in w]]


def inputs():
    line=[(0.,0.,0.),(10.,0.,0.)]
    corner=[*line,(10.,10.,0.)]
    square=[*corner,(0.,10.,0.)]
    for first in (0.,1e-13,1e-12,1.00001e-12,.1,2.,20.,1e6):
        for last in (0.,.1,2.,4.,20.):
            yield case(f'straight-width-{first}-{last}',widths=[(first,last)])
    for name,vertices in [('empty',[]),('singleton',line[:1]),('line',line),('corner',corner),('square',square),
        ('reflex',[(0.,0.,0.),(10.,0.,0.),(1.,1.,0.),(10.,4.,0.)]),
        ('bow',[(0.,0.,0.),(10.,10.,0.),(10.,0.,0.),(0.,10.,0.)]),
        ('duplicates',[(0.,0.,0.),(0.,0.,0.),(10.,0.,0.),(10.,0.,0.)]),
        ('collapsed',[(3.,4.,0.),(3.,4.,0.)])]:
        for closed in (False,True):
            count=len(vertices) if closed else max(0,len(vertices)-1)
            for width in (.1,2.,30.):
                for ramp in (False,True):
                    widths=[(width,width*(2. if ramp else 1.)) for _ in range(count)]
                    yield case(f'path-{name}-{closed}-{width}-{ramp}',vertices,closed,widths)
    for bulge in (-2.,-1.,-.4,-1e-12,0.,1e-12,.4,1.,2.):
        vertices=[(0.,0.,bulge),(2.,0.,0.)]
        for angle in (.1,.5,math.pi,0.,math.nan,math.inf):
            for widths in ([(.2,.2)],[(.2,.6)],[(3.,3.)]):
                yield case(f'arc-{bulge}-{angle}-{widths}',vertices,widths=widths,angle=angle)
    for bulges in ((1.,1.,1.,1.),(.4,0.,-.4,0.),(0.,.5,0.,.5)):
        vertices=[(v[0],v[1],b) for v,b in zip(square,bulges)]
        for closed in (False,True):
            yield case(f'curved-chain-{bulges}-{closed}',vertices,closed)
    for value in (-1.,math.nan,math.inf,-math.inf):
        for end in (0,1):
            width=[2.,2.];width[end]=value
            yield case(f'invalid-width-{value}-{end}',widths=[width])
    for count in (0,2,3):yield case(f'width-count-{count}',widths=[(2.,2.)]*count)
    for value in (math.nan,math.inf,-math.inf,1e200):
        for coordinate in range(6):
            vertices=[list(v) for v in line];vertices[coordinate//3][coordinate%3]=value
            yield case(f'nonfinite-source-{value}-{coordinate}',vertices)
    for origin in ((512345.678,4512345.678),(1e12,-1e12),(-0.,-0.)):
        for scale in (1e-5,1.,1e5):
            vertices=[(origin[0]+scale*v[0],origin[1]+scale*v[1],v[2]) for v in corner]
            yield case(f'translated-{origin}-{scale}',vertices,widths=[(scale,scale)]*2)
    for angle in (1e-12,1e-8,.001,math.pi-.001,math.pi,math.pi+.001):
        vertices=[(0.,0.,0.),(10.,0.,0.),(10.+10.*math.cos(angle),10.*math.sin(angle),0.)]
        yield case(f'join-{angle}',vertices)
    for step in (0.,1e-12,1e-9,1e-8,1.):
        yield case(f'width-discontinuity-{step}',corner,widths=[(2.,2.),(2.+step,2.)])
    rng=random.Random(953546502)
    for i in range(70):
        vertices=[(rng.uniform(-10.,10.),rng.uniform(-10.,10.),rng.choice((0.,0.,.2,-.2))) for _ in range(2+i%5)]
        closed=i%3==0
        count=len(vertices) if closed else len(vertices)-1
        widths=[(rng.uniform(.01,3.),rng.uniform(.01,3.)) for _ in range(count)]
        yield case(f'random-{i}',vertices,closed,widths,angle=.3)


def parsed(text):
    rows=iter(text.splitlines());header=next(rows).split();assert header[0]=='b'
    result=dict(length=CURVE['parsed'](header[1])[0],clone=int(header[4]),edges=[],stations=[])
    for kind,count in (('edges',int(header[2])),('stations',int(header[3]))):
        for _ in range(count):
            values=next(rows).split();assert values[0]==('e' if kind=='edges' else 's')
            assert len(values)==10
            record=tuple(int(v) if i==4 else CURVE['parsed'](v)[0] for i,v in enumerate(values[1:]))
            result[kind].append(record)
    assert next(rows,None) is None
    result['edges'].sort(key=lambda r:tuple(ARR['total_key'](v) if isinstance(v,float) else v for v in r))
    return result


def compare(actual,expected,scale,exact=False):
    # Miter intersections can be much farther away than the input endpoints.
    # Include the reference output geometry in its own floating-point scale.
    scale=max([scale,*[abs(v) for kind in ('edges','stations') for record in expected[kind] for v in record[:4] if math.isfinite(v)]])
    world_slack=16*math.ulp(scale) if math.isfinite(scale) else 0.
    def number(a,b,coordinate=False,absolute=0.):
        slack=max(world_slack if coordinate else 0.,absolute)
        assert CURVE['equal'](a,b,exact=exact,absolute=slack),(a,b,slack,exact)
    number(actual['length'],expected['length'])
    assert actual['clone']==expected['clone']
    fields=4
    # Source stations have deterministic source order and are not graph-welded.
    for kind in ('stations',):
        assert len(actual[kind])==len(expected[kind]),(kind,len(actual[kind]),len(expected[kind]))
        for position,(a,b) in enumerate(zip(actual[kind],expected[kind])):
            for index,(x,y) in enumerate(zip(a,b)):
                try:
                    if index==4:assert x==y,(x,y)
                    else:number(x,y,index<4)
                except AssertionError as error:raise AssertionError(f'{kind}[{position}][{index}]: {error}') from error
                fields+=1
    assert len(actual['edges'])==len(expected['edges']),('edge count',len(actual['edges']),len(expected['edges']))

    def projected_slack(edge,index):
        if exact or not all(math.isfinite(v) for v in edge[5:]):return 0.
        bound=0.
        for station in expected['stations']:
            if station[4]!=edge[4] or not all(math.isfinite(v) for v in station[:4]+station[5:]):continue
            rounding=16*math.ulp(max(abs(v) for v in station[5:]))
            if not all(station[5]-rounding<=v<=station[6]+rounding for v in edge[5:7]):continue
            dx,dy=station[2]-station[0],station[3]-station[1]
            squared=dx*dx+dy*dy
            if squared<=2**-52:continue # source parameter is exactly zero
            span=station[6]-station[5] if index<7 else station[8]-station[7]
            # Project coordinate rounding through dot(delta,along)/|along|²,
            # then through the original analytic station interval.
            propagated=world_slack*((abs(dx)+abs(dy))/squared)*abs(span)+rounding
            if math.isfinite(propagated):bound=max(bound,propagated)
        return bound

    def record_matches(a,b):
        if a[4]!=b[4]:return False
        try:
            for index,(x,y) in enumerate(zip(a,b)):
                if index!=4:number(x,y,index<4,projected_slack(b,index) if index>4 else 0.)
        except AssertionError:return False
        return True

    # The unordered outside-edge map may shift welded representatives by a
    # rounding. Sorting exact coordinates would pair unrelated edges at ties.
    # Require a one-to-one match of every oriented edge and ALL its metadata.
    candidates=[[j for j,b in enumerate(expected['edges']) if record_matches(a,b)] for a in actual['edges']]
    matched={}
    def assign(index,visited):
        for target in candidates[index]:
            if target in visited:continue
            visited.add(target)
            if target not in matched or assign(matched[target],visited):
                matched[target]=index
                return True
        return False
    for index in sorted(range(len(candidates)),key=lambda i:len(candidates[i])):
        assert assign(index,set()),('unmatched oriented edge',actual['edges'][index],expected['edges'])
    fields+=9*len(actual['edges'])
    return fields


def rust_driver(source,output):
    driver,paths=ARR['rust_driver'](source,output)
    path='src/geom2d/band.rs'
    run(['git','-c',f'safe.directory={source.as_posix()}','-C',source,'diff','--exit-code',CURVE['PIN'],'--',path])
    content=driver.read_text(encoding='utf-8').replace('mod geom2d {\n',f'mod geom2d {{\n#[path=r"{(source/path).as_posix()}"] pub mod band;\npub use polyline::{{BulgeArc,Polyline}};\n')
    content=content.replace('tests/cadkernel/arrangement2/reference.rs','tests/cadkernel/band2/reference.rs')
    driver.write_text(content,encoding='utf-8')
    return driver,[*paths,path]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--compiler',type=Path,default=ROOT/'build/dynlex.exe')
    parser.add_argument('--rustc',type=Path,default=Path.home()/'.rustup/toolchains/stable-x86_64-pc-windows-msvc/bin/rustc.exe')
    parser.add_argument('--case',default='')
    parser.add_argument('--output',type=Path)
    parser.add_argument('--skip-build',action='store_true')
    args=parser.parse_args()
    output=args.output or Path(tempfile.mkdtemp(prefix='band2-',dir=ROOT/'build'))
    output.mkdir(parents=True,exist_ok=True)
    digest=hashlib.sha256(args.compiler.read_bytes()).hexdigest()
    _,version,_=run([args.rustc,'-vV'])
    if os.name=='nt':assert 'host: x86_64-pc-windows-msvc' in version
    source=args.source.resolve();driver,paths=rust_driver(source,output)
    assert re.findall(r'#\[test\]\s*fn (\w+)',(source/'src/geom2d/band.rs').read_text(encoding='utf-8'))==[]
    assert len((ROOT/'tests/cadkernel/band2/expected.txt').read_text().splitlines())==6
    if not args.skip_build:
        _,comparison_tests,_=run([sys.executable,ROOT/'tests/cadkernel/band2/test_comparison.py'])
        (output/'comparison-tests.txt').write_text(comparison_tests)
        print('Six comparison-harness tests passed',flush=True)
        for mode in ('O0','O2'):
            options=[args.rustc,'--edition=2021','--crate-name','band_reference',driver,'-C',f'opt-level={mode[1]}','-A','dead_code']
            compile_program([*options,'-o',output/f'reference-{mode}.out'])
            for fixture in ('band2','band2_ownership'):
                print(COMMON['verify_fixture'](ROOT/'tests/cadkernel'/fixture,mode,args.compiler,output,60,30,False),flush=True)
            compile_program([args.compiler,ROOT/'tests/cadkernel/band2/probe.dl',f'-{mode}','-o',output/f'probe-{mode}.out'])
            for name,fragment in [('wrong_width_count','fixed array containing 3 items'),('wrong_precision','32-bit floating-point number')]:
                binary=output/f'{name}-{mode}.out'
                status,text,_=COMMON['run_process']([str(args.compiler),f'tests/cadkernel/band2/{name}.dl',f'-{mode}','-o',str(binary)],timeout=60,cwd=ROOT,phase='compilation')
                (output/f'{name}-{mode}.txt').write_text(text)
                assert status==1 and fragment in text and not binary.exists(),(status,text)
                print(f'{name}/{mode}: expected typed refusal',flush=True)
    count=fields=0
    source_rounding_cases=[]
    for name,values in inputs():
        if args.case not in name:continue
        arguments=[str(x) for x in values]
        coordinates=[v for i,v in enumerate(values[4:4+3*int(values[2])]) if i%3!=2]
        scale=max([0.,*[abs(v) for v in coordinates if math.isfinite(v)],*[abs(v) for v in values[4+3*int(values[2]):] if math.isfinite(v)]])
        (output/'current-input.json').write_text(json.dumps(dict(name=name,values=arguments)))
        outputs={}
        for prefix in ('reference','probe'):
            for mode in ('O0','O2'):
                _,text,_=run([output/f'{prefix}-{mode}.out',*arguments],timeout=30)
                (output/f'current-{prefix}-{mode}.txt').write_text(text)
                outputs[prefix,mode]=parsed(text)
        try:
            try:compare(outputs['reference','O0'],outputs['reference','O2'],scale,exact=True)
            except AssertionError:
                # HashMap iteration changes the second arrangement's input
                # order, including within repeated runs of the SAME Rust
                # binary. Canonical edge order alone cannot remove rounding
                # changes introduced by choosing a welded representative.
                compare(outputs['reference','O0'],outputs['reference','O2'],scale)
                source_rounding_cases.append(name)
            for mode in ('O0','O2'):fields+=compare(outputs['probe',mode],outputs['reference',mode],scale)
            compare(outputs['probe','O0'],outputs['probe','O2'],scale,exact=True)
        except AssertionError as error:raise AssertionError(f'{name}: {error}') from error
        count+=1
        if count%25==0:print(f'{count} cases passed ({name})',flush=True)
    assert count and hashlib.sha256(args.compiler.read_bytes()).hexdigest()==digest
    report=dict(revision=CURVE['PIN'],compiler_sha256=digest,rustc=version,source_tests=0,native_groups=6,cases=count,fields=fields,case_filter=args.case,exact_native_optimization_parity=True,exact_rust_optimization_parity=not source_rounding_cases,source_rounding_cases=source_rounding_cases)
    report['source_sha256']={p:hashlib.sha256((source/p).read_bytes()).hexdigest() for p in paths}
    report['native_source_sha256']={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [ROOT/'lib/cadkernel/band2.dl',ROOT/'lib/cadkernel/arrangement2.dl',ROOT/'lib/cadkernel/pair_index.dl',*sorted((ROOT/'tests/cadkernel/band2').glob('*.dl')),ROOT/'tests/cadkernel/band2_ownership/main.dl']}
    (output/('filtered-summary.json' if args.case else 'summary.json')).write_text(json.dumps(report,indent=2)+'\n')
    print(f'PASS: {count} cases, {fields} Rust comparisons; artifacts={output}',flush=True)


if __name__=='__main__':main()
