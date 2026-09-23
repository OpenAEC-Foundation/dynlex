"""Verify complete analytic bounds against unchanged pinned Rust source."""
from pathlib import Path
import argparse,hashlib,json,math,random,runpy,shutil,sys,tempfile

ROOT=Path(__file__).resolve().parents[3]
HERE=Path(__file__).resolve().parent
fixtures=runpy.run_path(str(ROOT/'tests/cadkernel/verify.py'))
curves=runpy.run_path(str(ROOT/'tests/cadkernel/curve2/verify.py'))
PIN=curves['PIN']

def run(command,timeout=90):
    status,out,_=fixtures['run_process']([str(x) for x in command],timeout=timeout,cwd=ROOT)
    assert status==0,(status,command,out)
    return out

def build(command):
    out=run(command)
    assert not out.strip(),out

def shape(kind=0,centre=(2.,3.),end=(5.,6.),radius=3.,minor=1.,axis=(1.,0.),first=.2,last=1.4,closed=False,vertices=()):
    return [kind,*centre,*end,radius,minor,*axis,first,last,int(closed),len(vertices),*(x for v in vertices for x in v)]

def cases():
    yield 'empty',[]
    for kind in range(8):
        base=shape(kind)
        yield f'kind {kind}',[base]
        for field in range(1,11):
            for value in (-0.,0.,-1.,1e-200,1e154,1e308,math.inf,-math.inf,math.nan):
                data=base.copy();data[field]=value
                yield f'field {kind} {field} {value}',[data]
    for first,last in ((0.,0.),(0.,2*math.pi),(-3.,3.),(3.,-3.),(-1e15,1e15),(2.,2.+1e-10)):
        for kind in (2,3):yield f'span {kind} {first} {last}',[shape(kind,first=first,last=last,axis=(.6,.8))]
    for bulge in (-math.inf,-1e300,-2.,-1.,-1e-12,-0.,0.,1e-12,.4,1.,2.,1e300,math.inf,math.nan):
        for closed in (False,True):
            for points in ((),((0.,0.,bulge),),((-1.,0.,bulge),(1.,0.,0.)),((0.,0.,bulge),(1e308,0.,0.)),((0.,0.,bulge),(math.nan,0.,0.))):
                yield f'polyline {bulge} {closed} {points}',[shape(4,closed=closed,vertices=points)]
    parts=[shape(),shape(1),shape(4),shape(5),shape(6),shape(radius=math.nan),shape(1,radius=-1.)]
    for i,a in enumerate(parts):
        for j,b in enumerate(parts):yield f'collection {i} {j}',[a,b]
    for start,end in (((-0.,-0.),(0.,0.)),((0.,0.),(-0.,-0.))):
        yield f'zero line {start} {end}',[shape(centre=start,end=end)]
    rng=random.Random(95354613)
    for i in range(240):
        collection=[]
        for _ in range(1+i%4):
            kind=rng.randrange(5)
            points=[(rng.uniform(-8,8),rng.uniform(-8,8),rng.uniform(-2,2)) for _ in range(i%6)]
            collection.append(shape(kind,centre=(rng.uniform(-20,20),rng.uniform(-20,20)),end=(rng.uniform(-20,20),rng.uniform(-20,20)),radius=rng.uniform(.01,8.),minor=rng.uniform(.01,8.),axis=(rng.uniform(-3,3),rng.uniform(-3,3)),first=rng.uniform(-20,20),last=rng.uniform(-20,20),closed=bool(i%2),vertices=points))
        yield f'random {i}',collection

def equal(a,b,exact=False):
    if a==b:return a!=0 or math.copysign(1,a)==math.copysign(1,b)
    if exact or a==0 or b==0:return False
    return math.isfinite(a) and math.isfinite(b) and math.isclose(a,b,rel_tol=3e-12,abs_tol=0.)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',required=True,type=Path)
    p.add_argument('--limit',type=int)
    args=p.parse_args()
    source=args.source.resolve()
    directory=Path(tempfile.mkdtemp(prefix='bounds2-',dir=ROOT/'build'))
    driver=curves['rust_driver'](source,directory)
    run(['git','-c',f'safe.directory={source.as_posix()}','-C',source,'diff','--exit-code','HEAD','--','src/geom2d/bounds.rs'])
    text=driver.read_text(encoding='utf-8')
    text=text.replace('mod geom2d {\n',f'mod geom2d {{\n#[path=r"{(source/"src/geom2d/bounds.rs").as_posix()}"] pub mod bounds;\npub use polyline::{{Polyline,PolylineVertex}};\n')
    text=text.replace((ROOT/'tests/cadkernel/curve2/reference.rs').as_posix(),(HERE/'reference.rs').as_posix())
    driver.write_text('#![allow(dead_code,unused_imports)]\n'+text,encoding='utf-8')
    if sys.platform=='win32':
        rustup=shutil.which('rustup') or Path.home()/'.cargo/bin/rustup.exe'
        rustc=[rustup,'run','stable-x86_64-pc-windows-msvc','rustc']
    else:rustc=[shutil.which('rustc')]
    version=run([*rustc,'--version','--verbose'])
    reference=directory/'reference.out'
    original=directory/'original.out'
    build([*rustc,'--edition=2021','--crate-name','bounds_reference',driver,'-O','-o',reference])
    build([*rustc,'--edition=2021','--crate-name','bounds_tests','--test',driver,'-O','-o',original])
    out=run([original,'geom2d::bounds::tests::'])
    assert '3 passed' in out,out
    print('Three unchanged Rust tests passed',flush=True)
    compiler=ROOT/'build'/('dynlex.exe' if sys.platform=='win32' else 'dynlex')
    programs=[reference]
    for level in ('O0','O2'):
        print(level,fixtures['verify_fixture'](ROOT/'tests/required/cadkernel_bounds2',level,compiler,directory,60,30,False),flush=True)
        binary=directory/f'probe-{level}.out'
        build([compiler,HERE/'probe.dl',f'-{level}','-o',binary])
        programs.append(binary)
    inputs=list(cases()); selected=inputs if args.limit is None else inputs[:args.limit]
    (directory/'cases.json').write_text(json.dumps(selected,indent=2),encoding='utf-8')
    comparisons=0
    for checked,(label,collection) in enumerate(selected,1):
        values=[len(collection),*(v for entry in collection for v in entry)]
        results=[[float(x) for x in run([program,*values]).splitlines()] for program in programs]
        wanted,o0,o2=results
        assert len(wanted)==len(o0)==len(o2),(label,results,collection,directory)
        assert wanted[0] in (0.,1.) and len(wanted)==1+4*int(wanted[0]),wanted
        for i,expected in enumerate(wanted):
            for actual in (o0[i],o2[i]):
                assert equal(actual,expected,exact=i==0),(label,i,expected,actual,collection,directory)
                comparisons+=1
            assert equal(o0[i],o2[i],exact=True),(label,'optimization',i,results,directory)
        if checked%100==0:print(f'{checked} inputs passed',flush=True)
    report={'source':PIN,'rustc':version,'compiler_sha256':hashlib.sha256(compiler.read_bytes()).hexdigest(),'cases':len(selected),'total_cases':len(inputs),'comparisons':comparisons,'complete':len(selected)==len(inputs),'exact_optimization_parity':True}
    (directory/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2),directory,flush=True)

if __name__=='__main__':main()
