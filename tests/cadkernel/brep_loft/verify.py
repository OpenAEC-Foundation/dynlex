# SPDX-License-Identifier: MPL-2.0
"""Verify both complete native loft paths and the shared profile/path senses."""
import argparse,hashlib,json,math,os,random,runpy,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
CROSS=runpy.run_path(str(ROOT/'tests/cadkernel/cross2/verify.py'))
CURVE,MEASURE,COMMON=(CROSS[k] for k in ('CURVE','MEASURE','COMMON'))
run,compile_program=(CROSS[k] for k in ('run','compile_program'))
HERE=Path(__file__).resolve().parent

def line(a,b):return [0,*a,*b]
def nurbs(points,degree=1,knots=(0,0,1,1),weights=(1,1)):
    return [3,degree,len(points),*[v for p in points for v in p],len(knots),*knots,len(weights),*weights]
def polygon(points=((-2,-1),(2,-1),(2,1),(-2,1)),shift=0,reverse=False,mask=0,spline=False):
    points=list(points);points=points[shift:]+points[:shift]
    if reverse:points.reverse()
    result=[]
    for i,a in enumerate(points):
        b=points[(i+1)%len(points)]
        if mask>>i&1:a,b=b,a
        result.append(nurbs([a,b]) if spline else line(a,b))
    return result
def ellipse(rx=2,ry=1,count=4,shift=0,reverse=False,kind=2):
    pieces=[]
    for i in range(count):
        a=math.tau*i/count;b=math.tau*(i+1)/count
        pieces.append([2,0,0,rx,ry,1,0,a,b] if kind==2 else [1,0,0,rx,a,b])
    pieces=pieces[shift:]+pieces[:shift]
    if reverse:pieces.reverse()
    return pieces
def frame(z=0,x=0,y=0,angle=0,sx=1,sy=1,tilt=0):
    c=math.cos(angle);s=math.sin(angle)
    return [x,y,z,c*sx,s*sx,0,-s*sy,c*sy,tilt]
def case(name,sections,ordered=False):
    return name,[int(ordered),len(sections),*[v for f,p in sections for v in [*f,len(p),*[x for c in p for x in c]]]]
def inputs():
    for ordered in (False,True):
        for count in (0,1,2,3,5):
            yield case(f'section-count-{ordered}-{count}',[(frame(z=i*3),polygon()) for i in range(count)],ordered)
        for shift in range(4):
            for reverse in (False,True):
                for mask in (0,1,3,7,15):
                    yield case(f'polygon-alignment-{ordered}-{shift}-{reverse}-{mask}',[(frame(),polygon()),(frame(z=3),polygon(shift=shift,reverse=reverse,mask=mask))],ordered)
        for angle in (0,1e-10,.2,math.pi/4,math.pi/2,math.pi):
            yield case(f'twist-{ordered}-{angle}',[(frame(),polygon()),(frame(z=3,angle=angle),polygon())],ordered)
    for kind in (1,2):
        for count in (1,2,3,4,5,8):
            for shift in (0,1):
                for reverse in (False,True):
                    p=ellipse(count=count,kind=kind)
                    q=ellipse(rx=3,ry=2,count=count,shift=shift,reverse=reverse,kind=kind)
                    yield case(f'conic-{kind}-{count}-{shift}-{reverse}',[(frame(),p),(frame(z=4),q)])
    for spline in (False,True):
        for zs in ((0,3),(3,0),(0,1,2),(0,2,1),(0,1,0),(0,0,2),(0,1e-9),(0,1.0001e-9)):
            yield case(f'progress-{spline}-{zs}',[(frame(z=z),polygon(spline=spline)) for z in zs])
    p=polygon()
    invalid=[[],[line((0,0),(1,0))],[line((0,0),(1,0)),line((1,0),(0,0))],polygon(((0,0),(1,0),(2,0))),polygon(((0,0),(0,0),(1,1))),[line((0,0),(1,0)),line((1,0),(0,1)),line((0,2),(0,0))],polygon(((0,0),(1,0),(0,1))),[[4,0,0,1]],[[6,0,0,1,0],line((1,0),(0,1)),line((0,1),(0,0))],[[7,0,0,1,0],line((1,0),(0,1)),line((0,1),(0,0))]]
    for i,q in enumerate(invalid):
        yield case(f'invalid-profile-{i}',[(frame(),p),(frame(z=3),q)])
        yield case(f'invalid-both-{i}',[(frame(),q),(frame(z=3),q)])
    mixed=[[1,0,0,2,0,math.pi/2],line((0,2),(0,0)),line((0,0),(2,0))]
    yield case('mixed-arcs-lines',[(frame(),mixed),(frame(z=4),mixed)])
    for i,delta in enumerate((-1e-9,-.99999e-9,0,.99999e-9,1e-9,1.00001e-9,1e-8)):
        q=polygon();q[1][1]+=delta
        yield case(f'joining-tolerance-{i}',[(frame(),q),(frame(z=3),q)])
    for p in (polygon(),ellipse()):
        tag=p[0][0]
        for value in (-math.inf,-1e308,-1e12,-0.,1e-300,1e-12,1e9,1e12,1e150,1e308,math.inf,math.nan):
            for axis in (0,2,3,4,5,6,8):
                f=frame(z=3);f[axis]=value
                yield case(f'frame-special-{tag}-{axis}-{value}',[(frame(),p),(f,p)])
        for origin in (1e6,1e9,1e12):
            yield case(f'translated-{tag}-{origin}',[(frame(x=origin,y=origin),p),(frame(z=3,x=origin,y=origin),p)])
    for weights in ((1,1),(2,1),(0,1),(-1,1),(math.inf,1),(math.nan,1)):
        p=polygon(spline=True);p[0]=nurbs([(-2,-1),(2,-1)],weights=weights)
        yield case(f'nurbs-weights-{weights}',[(frame(),p),(frame(z=3),p)])
    for knots in ((2,2,6,6),(0,0,2,2),(0,0,0,0),(0,2,1,3),(math.nan,0,1,1)):
        p=polygon(spline=True);q=polygon(spline=True);q[0]=nurbs([(-2,-1),(2,-1)],knots=knots)
        yield case(f'nurbs-knots-{knots}',[(frame(),p),(frame(z=3),q)])
    p=polygon(spline=True);q=polygon(spline=True);q[0]=nurbs([(-2,-1),(0,-2),(2,-1)],degree=2,knots=(0,0,0,1,1,1),weights=(1,1,1))
    yield case('incompatible-degree',[(frame(),p),(frame(z=3),q)])
    inverted=[nurbs([(0,0),(1,1)],knots=(0,2,1,3))]
    yield case('ordered-rejects-before-evaluation',[(frame(),inverted),(frame(z=3),inverted)],ordered=2)
    rng=random.Random(310796)
    for i in range(70):
        curved=i%2==0;n=rng.randrange(3,8);count=rng.randrange(2,5)
        sections=[]
        for j in range(count):
            p=ellipse(rx=rng.uniform(.3,4),ry=rng.uniform(.3,4),count=n) if curved else polygon([(math.cos(math.tau*k/n)*2,math.sin(math.tau*k/n)) for k in range(n)])
            sections.append((frame(z=j*3,x=rng.uniform(-.5,.5),y=rng.uniform(-.5,.5),angle=rng.uniform(-.4,.4),sx=rng.uniform(.5,2),sy=rng.uniform(.5,2),tilt=rng.uniform(-.5,.5)),p))
        yield case(f'random-{i}',sections,ordered=i%3==0)

def driver(source,output):
    sweep=(source/'src/brep/sweep.rs').read_text(encoding='utf8')
    start=sweep.index('fn path_senses(');end=sweep.index('/// The edges of one copy',start)
    excerpt=sweep[start:end]
    target=output/'profile-source.rs'
    target.write_text('use crate::geom2d::Curve as Curve2;\n'+excerpt+'\npub fn export_path_senses(p:&[Curve2])->Option<Vec<bool>>{path_senses(p)}\npub fn export_profile_senses(p:&[Curve2])->Option<Vec<bool>>{profile_senses(p)}\n')
    contents='pub use cadkernel::{space,geom2d};\nmod brep {\npub use cadkernel::brep::{geometry,topology,Provenance,SurfaceKey};\n'
    for name,path in [('nurbs_builder',source/'src/brep/nurbs_builder.rs'),('sweep',target),('loft',source/'src/brep/loft.rs')]:
        contents+=f'#[path=r"{path.as_posix()}"] pub mod {name};\n'
    contents+='}\n'+f'include!(r"{(HERE/"reference.rs").as_posix()}");\n'
    target=output/'driver.rs';target.write_text(contents);return target

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path)
    p.add_argument('--compiler',type=Path,default=ROOT/'build/dynlex.exe')
    p.add_argument('--rustc',type=Path,default=Path.home()/'.rustup/toolchains/stable-x86_64-pc-windows-msvc/bin/rustc.exe')
    p.add_argument('--libraries',type=Path,default=ROOT/'build/brep-make-six-checks')
    p.add_argument('--dependencies',type=Path,default=ROOT/'build/topology-reference-deps/target/debug/deps')
    p.add_argument('--skip-build',action='store_true');p.add_argument('--case',default='');p.add_argument('--skip-fixtures',action='store_true')
    a=p.parse_args();source=a.source.resolve();out=(a.output or Path(tempfile.mkdtemp(prefix='brep-loft-',dir=ROOT/'build'))).resolve();out.mkdir(parents=True,exist_ok=True)
    make=runpy.run_path(str(ROOT/'tests/cadkernel/brep_make/verify.py'));paths=make['rust_source'](source)
    digest=hashlib.sha256(a.compiler.read_bytes()).hexdigest();_,version,_=run([a.rustc,'-vV'])
    if os.name=='nt':assert 'host: x86_64-pc-windows-msvc' in version
    harness=driver(source,out)
    if not a.skip_build:
        for mode in ('O0','O2'):
            compile_program([a.rustc,'--edition=2021',harness,'--extern',f'cadkernel={(a.libraries/f"libcadkernel-{mode}.rlib").resolve()}','-L',f'dependency={a.dependencies.resolve()}','-C',f'opt-level={mode[1]}','-A','dead_code','-o',out/f'reference-{mode}.out'])
            if not a.skip_fixtures:
                for fixture in ('brep_loft','brep_loft_geometry','brep_loft_ownership','brep_loft_ordered_invalid'):
                    print(COMMON['verify_fixture'](ROOT/'tests/cadkernel'/fixture,mode,a.compiler,out,120,30,False),flush=True)
            status,text,elapsed=COMMON['run_process']([str(a.compiler),str(HERE/'probe.dl'),f'-{mode}','-o',str(out/f'probe-{mode}.out')],timeout=120,cwd=ROOT,phase='compilation')
            (out/f'probe-{mode}-compile.txt').write_text(text);assert status==0 and not text.strip(),text
            print(f'probe-{mode}: compile={elapsed:.3f}s',flush=True)
            for invalid in ('wrong_profile','wrong_sections'):
                code,text,_=COMMON['run_process']([str(a.compiler),str(HERE/f'{invalid}.dl'),f'-{mode}','-o',str(out/f'{invalid}-{mode}.out')],timeout=60,cwd=ROOT,phase='compilation')
                (out/f'{invalid}-{mode}.txt').write_text(text)
                assert code!=0 and 'check cad loft' in text,(invalid,mode,code,text)
    count=fields=0
    source_panics=[]
    for name,values in inputs():
        if a.case not in name:continue
        args=[str(x) for x in values];(out/'current-input.json').write_text(json.dumps(dict(name=name,values=args)))
        if name=='nurbs-knots-(0, 2, 1, 3)':
            # This permissively constructed curve has an inverted active domain.
            # The existing evaluators terminate before loft can return an Option.
            for mode in ('O0','O2'):
                code,text,_=COMMON['run_process']([str(out/f'reference-{mode}.out'),*args],timeout=30,cwd=ROOT,phase='runtime')
                assert code==101 and 'min > max' in text,(name,mode,code,text)
                code,text,_=COMMON['run_process']([str(out/f'probe-{mode}.out'),*args],timeout=30,cwd=ROOT,phase='runtime')
                assert code in (3,134,-6),(name,mode,code,text)
            source_panics.append(name)
            continue
        references=[];actuals=[]
        for mode in ('O0','O2'):
            _,text,_=run([out/f'reference-{mode}.out',*args],timeout=30);expected,limits=MEASURE['expected_values'](text)
            (out/f'current-rust-{mode}.txt').write_text(text);references.append(expected)
            _,text,_=run([out/f'probe-{mode}.out',*args],timeout=30);actual=CURVE['parsed'](text);actuals.append(actual)
            (out/f'current-native-{mode}.txt').write_text(text)
            assert len(actual)==len(expected),f'{name}/{mode}: {len(actual)} != {len(expected)} fields'
            for i,(x,y,limit) in enumerate(zip(actual,expected,limits)):
                assert CURVE['equal'](x,y,exact=limit<0,absolute=max(limit,0.)),f'{name}/{mode} field {i}: {x} != {y}'
            fields+=len(expected)
        for label,outputs in [('Rust',references),('native',actuals)]:
            assert len(outputs[0])==len(outputs[1]) and all(CURVE['equal'](x,y,exact=True) for x,y in zip(*outputs)),f'{name}: {label} optimization divergence'
        count+=1
        if count%25==0:print(f'{count} cases passed ({name})',flush=True)
    assert count and hashlib.sha256(a.compiler.read_bytes()).hexdigest()==digest
    fixtures=('brep_loft','brep_loft_geometry','brep_loft_ownership','brep_loft_ordered_invalid')
    assert '#[test]' not in (source/'src/brep/loft.rs').read_text()
    groups=sum(len((ROOT/'tests/cadkernel'/f/'expected.txt').read_text().splitlines()) for f in fixtures)
    report=dict(revision=CURVE['PIN'],compiler_sha256=digest,rustc=version,cases=count,fields=fields,case_filter=a.case,exact_native_optimization_parity=True,exact_rust_optimization_parity=True,source_unit_groups=0,native_groups=groups,fixtures_checked=not(a.skip_fixtures or a.skip_build),type_refusal_checks=0 if a.skip_build else 4)
    report['source_panics_with_matching_native_abort']=source_panics
    report['source_sha256']={p:hashlib.sha256((source/p).read_bytes()).hexdigest() for p in paths}
    report['library_sha256']={mode:hashlib.sha256((a.libraries/f'libcadkernel-{mode}.rlib').read_bytes()).hexdigest() for mode in ('O0','O2')}
    native_paths=[ROOT/'lib/cadkernel/brep_loft.dl',ROOT/'lib/cadkernel/brep_profile.dl']
    native_paths += [p for f in fixtures for p in sorted((ROOT/'tests/cadkernel'/f).iterdir()) if p.suffix in ('.dl','.txt','.rs','.py')]
    report['native_source_sha256']={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in native_paths}
    (out/('filtered-summary.json' if a.case else 'summary.json')).write_text(json.dumps(report,indent=2)+'\n')
    print(f'PASS: {count} cases; {fields} Rust comparisons; artifacts={out}',flush=True)
if __name__=='__main__':main()
