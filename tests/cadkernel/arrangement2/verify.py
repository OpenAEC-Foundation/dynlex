# SPDX-License-Identifier: MPL-2.0
"""Compare split/weld/face traversal, edge provenance and crossings with Rust."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import random
import re
import runpy
import struct
import tempfile

ROOT=Path(__file__).resolve().parents[3]
CROSS=runpy.run_path(str(ROOT/'tests/cadkernel/cross2/verify.py'))
CURVE,COMMON=(CROSS[key] for key in ('CURVE','COMMON'))
run,compile_program=(CROSS[key] for key in ('run','compile_program'))


def case(name,lines=(),tol=1e-6,mode=0):
    return name,[mode,tol,len(lines),*[v for i,line in enumerate(lines) for v in (*line,2**64-1-i)]]


def polygon(points):
    return [(*p,*points[(i+1)%len(points)]) for i,p in enumerate(points)]


def rectangle(x=0.,y=0.,w=10.,h=6.):
    return polygon([(x,y),(x+w,y),(x+w,y+h),(x,y+h)])


def inputs():
    rng=random.Random(953546638)
    square=rectangle()
    for name,lines in [('empty',[]),('open',square[:2]),('square',square),
        ('duplicate',square+square),('reverse-duplicate',square+[(*s[2:],*s[:2]) for s in square]),
        ('split-duplicate',square+[(0.,0.,5.,0.),(5.,0.,10.,0.)]),
        ('overlap',rectangle()+rectangle(5.,3.)),('nested',square+rectangle(2.,2.,3.,2.)),
        ('disconnected',square+rectangle(20.,0.)),('spur-out',square+[(5.,6.,5.,9.)]),
        ('spur-in',square+[(5.,6.,5.,3.)]),('diagonals',square+[(0.,0.,10.,6.),(10.,0.,0.,6.)]),
        ('collapsed',square+[(3.,3.,3.,3.)]),('collinear',[(0.,0.,5.,0.),(3.,0.,8.,0.)]),
        ('hash',[(-1.,0.,11.,0.),(-1.,10.,11.,10.),(0.,-1.,0.,11.),(10.,-1.,10.,11.)]),
        ('long-thin',[(0.,0.,1e12,0.),(0.,1.,1e12,1.),(1.,-1.,1.,2.),(2.,-1.,2.,2.)])]:
        yield case(name,lines)
    for i in range(24):
        shuffled=[(*line[2:],*line[:2]) if rng.randrange(2) else line for line in square]
        rng.shuffle(shuffled)
        yield case(f'order-{i}',shuffled)
    for tol in (5e-324,1e-300,1e-20,1e-15,1e-10,1e-6,.1,1.,10.):
        for gap in (0.,1e-12,1e-7,1e-6,.01):
            lines=square.copy();lines[1]=(10.,gap,10.,6.)
            yield case(f'gap-{tol}-{gap}',lines,tol)
    for n in (2,3,5,10,20):
        lines=[(float(i),-1.,float(i),float(n+1)) for i in range(n+1)]
        lines += [(-1.,float(i),float(n+1),float(i)) for i in range(n+1)]
        yield case(f'grid-{n}',lines)
        yield case(f'grid-reverse-{n}',[(*line[2:],*line[:2]) for line in reversed(lines)])
    for n in (3,10,40):
        yield case(f'distributed-{n}',[line for i in range(n) for line in rectangle(30.*i,0.)])
    for origin in ((0.,0.),(-0.,-0.),(512345.678,4512345.678),(1e12,-1e12),(-1e12,1e12)):
        for scale in (1e-7,1e-5,1.,1e6,1e140,1e200):
            lines=rectangle(*origin,10.*scale,6.*scale)
            yield case(f'scale-{origin}-{scale}',lines,max(5e-324,1e-6*scale))
    for value in (math.nan,math.inf,-math.inf,1e308):
        for coordinate in range(4):
            lines=square.copy();line=list(lines[0]);line[coordinate]=value;lines[0]=tuple(line)
            yield case(f'nonfinite-graph-{value}-{coordinate}',lines)
    for i in range(100):
        lines=rectangle()
        lines += [tuple(rng.uniform(-2.,12.) for _ in range(4)) for _ in range(i%11)]
        rng.shuffle(lines)
        yield case(f'random-graph-{i}',lines)
    for i in range(40):
        points=[(rng.randrange(-8,9)*1.,rng.randrange(-8,9)*1.) for _ in range(4+i%8)]
        yield case(f'integer-knots-{i}',polygon(points))
    a=(0.,0.,10.,0.)
    for length in (0.,1e-300,1e-9,math.sqrt(2**-52),math.sqrt(2**-52)*(1.+1e-14),1.,1e12,1e200):
        for shift in (-1.,0.,.5,1.,2.):
            yield case(f'cross-length-{length}-{shift}',[(0.,0.,length,0.),(length*shift,0.,length*(shift+1.),0.)],mode=1)
    for tol in (1e-15,1e-6,.1):
        for angle in (0.,1e-13,1e-12,1.00001e-12,1e-6,.5,math.pi):
            for offset in (0.,tol*.5,tol,tol*1.01,1.):
                b=(4.,offset,14.*math.cos(angle),offset+10.*math.sin(angle))
                yield case(f'cross-parallel-{tol}-{angle}-{offset}',[a,b],tol,1)
    for value in (-0.,math.nan,math.inf,-math.inf,1e308):
        for coordinate in range(8):
            pair=[0.,0.,10.,0.,5.,-1.,5.,1.];pair[coordinate]=value
            yield case(f'cross-nonfinite-{value}-{coordinate}',[tuple(pair[:4]),tuple(pair[4:])],mode=1)
    for i in range(100):
        yield case(f'cross-random-{i}',[tuple(rng.uniform(-10.,10.) for _ in range(4)) for _ in range(2)],mode=1)
    for points in ([],[(1.,2.)],[(0.,0.),(1.,1.)],[(0.,0.),(3.,0.),(0.,2.)],
        [(512345.678,4512345.678),(512355.678,4512345.678),(512355.678,4512355.678),(512345.678,4512355.678)],
        [(0.,0.),(math.nan,1.),(1.,1.)],[(0.,0.),(math.inf,1.),(1.,1.)]):
        for reverse in (False,True):
            for closed in (False,True):
                ring=points[::-1] if reverse else points[:]
                if closed and ring:ring.append(ring[0])
                yield case(f'area-{points}-{reverse}-{closed}',[(*p,*p) for p in ring],mode=2)


def total_key(value):
    bits=struct.unpack('>Q',struct.pack('>d',value))[0]
    return (~bits)&(2**64-1) if bits>>63 else bits^(1<<63)


def canonical(rings):
    result=[]
    for area,ring in rings:
        def key(vertex):
            return tuple(total_key(x) for x in vertex[:-1])+((vertex[-1],) if len(vertex)==5 else (total_key(vertex[-1]),))
        rotations=[ring[i:]+ring[:i] for i in range(len(ring))]
        chosen=min(rotations,key=lambda r:tuple(map(key,r)))
        result.append((area,chosen))
    return sorted(result,key=lambda r:tuple(tuple(total_key(x) if isinstance(x,float) else x for x in v) for v in r[1]))


def parsed(text):
    def scalar(token):
        return CURVE['parsed'](token)[0]
    rows=iter(text.splitlines())
    first=next(rows).split();assert first[0]=='a'
    result={'area':scalar(first[1])}
    for row in rows:
        tokens=row.split();kind=tokens[0]
        if kind=='c':
            result[kind]=[int(tokens[1]),*[scalar(v) for v in tokens[2:6]],int(tokens[6])]
        else:
            assert kind in ('f','t'),row
            rings=[]
            for _ in range(int(tokens[1])):
                header=next(rows).split();assert header[0]=='r'
                vertices=[]
                for _ in range(int(header[1])):
                    vertex=next(rows).split();assert vertex[0]==('p' if kind=='f' else 'e')
                    vertices.append(tuple(scalar(v) for v in vertex[1:]) if kind=='f' else (*map(scalar,vertex[1:5]),int(vertex[5])))
                assert len(vertices)>=3
                rings.append((scalar(header[2]),vertices))
            result[kind]=canonical(rings)
    return result


def compare(actual,expected,scale,exact=False,zero_sign_fields=()):
    assert actual.keys()==expected.keys(),(actual.keys(),expected.keys())
    def number(a,b,coordinate=False):
        absolute=16*math.ulp(scale) if coordinate and math.isfinite(scale) else 0.
        assert CURVE['equal'](a,b,exact=exact,absolute=absolute),(a,b,absolute,exact)
    fields=1
    number(actual['area'],expected['area'])
    if 'c' in expected:
        a,b=actual['c'],expected['c'];assert a[0]==b[0] and a[-1]==b[-1],(a,b)
        for index,(x,y) in enumerate(zip(a[1:-1],b[1:-1]),1):
            if index in zero_sign_fields:
                assert x==0. and y==0.,(index,x,y)
            else:number(x,y)
        fields+=6
    for kind in ('f','t'):
        if kind not in expected:continue
        assert len(actual[kind])==len(expected[kind]),(kind,'face count',len(actual[kind]),len(expected[kind]))
        fields+=1
        for (a,ar),(b,br) in zip(actual[kind],expected[kind]):
            number(a,b);assert len(ar)==len(br),(kind,'ring length',len(ar),len(br))
            fields+=2
            for av,bv in zip(ar,br):
                assert len(av)==len(bv)
                for index,(x,y) in enumerate(zip(av,bv)):
                    if kind=='t' and index==4:assert x==y,(x,y)
                    else:number(x,y,True)
                    fields+=1
    return fields


def rust_driver(source,output):
    driver=CROSS['rust_driver'](source,output)
    path='src/geom2d/arrangement.rs'
    run(['git','-c',f'safe.directory={source.as_posix()}','-C',source,'diff','--exit-code',CURVE['PIN'],'--',path])
    content=driver.read_text().replace('mod geom2d {\n',f'mod geom2d {{\n#[path=r"{(source/path).as_posix()}"] pub mod arrangement;\n')
    content=content.replace('tests/cadkernel/cross2/reference.rs','tests/cadkernel/arrangement2/reference.rs')
    driver.write_text(content)
    paths=re.findall(r'#\[path=r"([^"]+)"\]',content)
    return driver,[str(Path(p).relative_to(source)).replace('\\','/') for p in paths]+['src/geom2d/mod.rs']


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--compiler',type=Path,default=ROOT/'build/dynlex.exe')
    parser.add_argument('--rustc',type=Path,default=Path.home()/'.rustup/toolchains/stable-x86_64-pc-windows-msvc/bin/rustc.exe')
    parser.add_argument('--case',default='')
    parser.add_argument('--output',type=Path)
    parser.add_argument('--skip-build',action='store_true')
    args=parser.parse_args()
    output=args.output or Path(tempfile.mkdtemp(prefix='arrangement2-',dir=ROOT/'build'))
    output.mkdir(parents=True,exist_ok=True)
    digest=hashlib.sha256(args.compiler.read_bytes()).hexdigest()
    _,version,_=run([args.rustc,'-vV'])
    if os.name=='nt':assert 'host: x86_64-pc-windows-msvc' in version
    source=args.source.resolve();driver,paths=rust_driver(source,output)
    names=re.findall(r'#\[test\]\s*fn (\w+)',(source/'src/geom2d/arrangement.rs').read_text(encoding='utf-8'))
    assert names==(ROOT/'tests/cadkernel/arrangement2/expected.txt').read_text().splitlines() and len(names)==18
    if not args.skip_build:
        for mode in ('O0','O2'):
            options=[args.rustc,'--edition=2021','--crate-name','arrangement_reference',driver,'-C',f'opt-level={mode[1]}','-A','dead_code']
            compile_program([*options,'-o',output/f'reference-{mode}.out'])
            compile_program([*options,'--test','-o',output/f'upstream-{mode}.out'])
            _,text,_=run([output/f'upstream-{mode}.out','geom2d::arrangement::tests::'])
            assert '18 passed; 0 failed' in text,text
            (output/f'upstream-{mode}.txt').write_text(text)
            for fixture in ('arrangement2','arrangement2_ownership'):
                print(COMMON['verify_fixture'](ROOT/'tests/cadkernel'/fixture,mode,args.compiler,output,60,30,False),flush=True)
            compile_program([args.compiler,ROOT/'tests/cadkernel/arrangement2/probe.dl',f'-{mode}','-o',output/f'probe-{mode}.out'])
    count=fields=0
    source_zero_sign_changes=[]
    for name,values in inputs():
        if args.case not in name:continue
        arguments=[str(x) for x in values]
        scale=max([0.,*[abs(float(v)) for i,v in enumerate(values[3:]) if i%5!=4 and math.isfinite(float(v))]])
        (output/'current-input.json').write_text(json.dumps(dict(name=name,values=arguments)))
        _,reference,_=run([output/'reference-O0.out',*arguments],timeout=30)
        expected=parsed(reference)
        (output/'current-rust.txt').write_text(reference)
        _,optimized,_=run([output/'reference-O2.out',*arguments],timeout=30)
        optimized=parsed(optimized)
        # The unchanged Rust min/max chain can select a different sign for an
        # equal-zero overlap bound after optimization. Allow only aStart/aEnd,
        # only when BOTH reference runs demonstrate that exact zero-sign change.
        # Each native mode must still match its corresponding reference sign.
        zero_sign_fields=[]
        if 'c' in expected and expected['c'][0]==optimized['c'][0]==2:
            for index in (1,2):
                a,b=expected['c'][index],optimized['c'][index]
                if a==b==0. and math.copysign(1.,a)!=math.copysign(1.,b):
                    zero_sign_fields.append(index)
                    source_zero_sign_changes.append(dict(case=name,field=('aStart' if index==1 else 'aEnd'),O0=str(a),O2=str(b)))
        compare(optimized,expected,scale,exact=True,zero_sign_fields=zero_sign_fields)
        outputs=[]
        for mode in ('O0','O2'):
            _,text,_=run([output/f'probe-{mode}.out',*arguments],timeout=30)
            (output/f'current-{mode}.txt').write_text(text)
            actual=parsed(text)
            try:fields+=compare(actual,expected if mode=='O0' else optimized,scale)
            except AssertionError as error:raise AssertionError(f'{name}/{mode}: {error}') from error
            outputs.append(actual)
        compare(*outputs,scale,exact=True,zero_sign_fields=zero_sign_fields)
        count+=1
        if count%25==0:print(f'{count} cases passed ({name})',flush=True)
    assert count and hashlib.sha256(args.compiler.read_bytes()).hexdigest()==digest
    report=dict(revision=CURVE['PIN'],compiler_sha256=digest,rustc=version,source_tests=18,cases=count,fields=fields,case_filter=args.case,exact_native_optimization_parity=not source_zero_sign_changes,exact_rust_optimization_parity=not source_zero_sign_changes,source_zero_sign_changes=source_zero_sign_changes,each_mode_preserves_reference_zero_signs=True)
    report['source_sha256']={p:hashlib.sha256((source/p).read_bytes()).hexdigest() for p in paths}
    report['native_source_sha256']={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [ROOT/'lib/cadkernel/arrangement2.dl',ROOT/'lib/cadkernel/pair_index.dl',*sorted((ROOT/'tests/cadkernel/arrangement2').glob('*.dl')),ROOT/'tests/cadkernel/arrangement2_ownership/main.dl']}
    (output/('filtered-summary.json' if args.case else 'summary.json')).write_text(json.dumps(report,indent=2)+'\n')
    print(f'PASS: {count} cases, {fields} Rust comparisons; artifacts={output}',flush=True)


if __name__=='__main__':main()
