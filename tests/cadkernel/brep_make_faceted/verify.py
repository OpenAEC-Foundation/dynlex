# SPDX-License-Identifier: MPL-2.0
"""Compare complete faceted solids, canonicalizing only unordered edge keys."""
import argparse
import hashlib
import itertools
import json
import math
from pathlib import Path
import random
import runpy
import tempfile

ROOT=Path(__file__).resolve().parents[3]
MAKE=runpy.run_path(str(ROOT/'tests/cadkernel/brep_make/verify.py'))
CURVE,MEASURE,COMMON=(MAKE[k] for k in ('CURVE','MEASURE','COMMON'))
run,compile_program=(MAKE[k] for k in ('run','compile_program'))
TETRA=[(0.,0.,0.),(1.,0.,0.),(0.,1.,0.),(0.,0.,1.)]
TRIANGLES=[[0,2,1],[0,1,3],[1,2,3],[2,0,3]]
BOX=[(float(i&1),float((i>>1)&1),float((i>>2)&1)) for i in range(8)]
QUADS=[[0,2,6,4],[1,5,7,3],[0,4,5,1],[2,3,7,6],[0,1,3,2],[4,6,7,5]]

def inputs():
    for points,rings in ((TETRA,TRIANGLES),(BOX,QUADS)):
        for mask in range(1<<len(rings)):
            yield f'winding-{len(rings)}-{mask}',points,[r[::-1] if mask&(1<<i) else r[:] for i,r in enumerate(rings)]
        for rotation in range(1,4):
            yield f'rotation-{len(rings)}-{rotation}',points,[r[rotation:]+r[:rotation] for r in rings]
        yield f'repeated-neighbours-{len(rings)}',points,[[r[0],*[v for v in r for _ in range(2)],r[0],r[0]] for r in rings]
        for face in range(len(rings)):
            yield f'open-{len(rings)}-{face}',points,rings[:face]+rings[face+1:]
            yield f'duplicate-{len(rings)}-{face}',points,rings+[rings[face]]
        for value in (-1, len(points),2147483647):
            copy=[r[:] for r in rings];copy[0][0]=value
            yield f'bad-index-{len(rings)}-{value}',points,copy
        for empty in ([],[0],[0,0],[0,1],[0,1,0],[0,0,0,0]):
            yield f'short-ring-{len(rings)}-{empty}',points,[empty,*rings[1:]]
        for extra in ((2.,3.,4.),(math.nan,0.,0.),(0.,math.inf,0.)):
            yield f'unused-point-{len(rings)}-{extra}',[*points,extra],rings
        for value in (-math.inf,-1e308,-1e150,-1.,-0.,0.,5e-324,1e-300,1e-13,1e-12,1e-6,1.,1e150,1e308,math.inf,math.nan):
            for axis in range(3):
                mutated=[list(p) for p in points];mutated[-1][axis]=value
                yield f'coordinate-{len(rings)}-{axis}-{value}',mutated,rings
        for height in (0.,1e-15,1e-13,1e-12,2e-12,1e-11,1e-8,1.,1e12):
            yield f'flat-{len(rings)}-{height}',[(x,y,z*height) for x,y,z in points],rings
        for scale in (1e-200,1e-100,1e-13,1e-12,1.0001e-12,1e-10,1e-4,1e3,1e50,1e100,1e150):
            yield f'scale-{len(rings)}-{scale}',[tuple(x*scale for x in p) for p in points],rings
        for delta in (1e-10,1e-9,1e-8,1.0001e-8,1e-7,.1):
            mutated=[list(p) for p in points];mutated[0][2]+=delta
            yield f'plane-gap-{len(rings)}-{delta}',mutated,rings
    for permutation in itertools.permutations(range(4)):
        yield f'face-order-{permutation}',TETRA,[TRIANGLES[i] for i in permutation]
    for n in range(4):
        yield f'few-vertices-{n}',TETRA[:n],TRIANGLES
        yield f'few-faces-{n}',TETRA,TRIANGLES[:n]
    yield 'empty',[],[]
    for offset,scale in ((3.,1.),(.1,.1),(0.,1.)):
        other=[tuple(offset+scale*x for x in p) for p in TETRA]
        yield f'disconnected-{offset}-{scale}',TETRA+other,TRIANGLES+[[i+4 for i in r] for r in TRIANGLES]
    yield 'repeated-corner',BOX,[[0,2,6,2,6,4],*QUADS[1:]]
    rng=random.Random(83991)
    for index in range(96):
        points,rings=(TETRA,TRIANGLES) if index%2==0 else (BOX,QUADS)
        offset=[rng.uniform(-1e6,1e6) if index%3==0 else rng.uniform(-50,50) for _ in range(3)]
        matrix=[[rng.uniform(-4,4) for _ in range(3)] for _ in range(3)]
        transformed=[[offset[j]+sum(p[k]*matrix[j][k] for k in range(3)) for j in range(3)] for p in points]
        face_order=list(range(len(rings)));rng.shuffle(face_order)
        yield f'affine-{index}',transformed,[rings[j][::-1] if rng.randrange(2) else rings[j] for j in face_order]

def encode(points,rings):
    return [len(points),*[x for p in points for x in p],len(rings),*[x for r in rings for x in (len(r),*r)]]

def verify_refusals(compiler,output):
    for mode in ('O0','O2'):
        for name in ('wrong_points','wrong_indices','wrong_rings'):
            path=ROOT/'tests/cadkernel/brep_make_faceted'/f'{name}.dl'
            status,text,elapsed=COMMON['run_process']([str(compiler),str(path),f'-{mode}','-o',str(output/f'{name}-{mode}.out')],timeout=60,cwd=ROOT)
            (output/f'{name}-{mode}.txt').write_text(text)
            assert status==1 and 'Error:' in text and ('No overload matches' in text or "couldn't be resolved" in text),(status,text)
            assert 'the cad faceted solid with vertices' in text,text
            print(f'{name}/{mode}: expected typed refusal ({elapsed:.3f}s)',flush=True)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',required=True,type=Path)
    parser.add_argument('--compiler',type=Path,default=ROOT/'build/dynlex.exe')
    parser.add_argument('--rustc',type=Path,default=Path.home()/'.rustup/toolchains/stable-x86_64-pc-windows-msvc/bin/rustc.exe')
    parser.add_argument('--dependencies',type=Path,default=ROOT/'build/topology-reference-deps/target/debug/deps')
    parser.add_argument('--libraries',type=Path)
    parser.add_argument('--output',type=Path)
    parser.add_argument('--skip-build',action='store_true')
    parser.add_argument('--case',default='')
    args=parser.parse_args()
    source=args.source.resolve();paths=MAKE['rust_source'](source)
    output=args.output or Path(tempfile.mkdtemp(prefix='brep-faceted-',dir=ROOT/'build'))
    output.mkdir(parents=True,exist_ok=True)
    compiler_hash=hashlib.sha256(args.compiler.read_bytes()).hexdigest()
    _,version,_=run([args.rustc,'-vV'])
    deps=args.dependencies.resolve();externs=[]
    for name in ('spade','rustc_hash'):
        found=list(deps.glob(f'lib{name}-*.rlib'));assert len(found)==1
        externs+=['--extern',f'{name}={found[0]}']
    if not args.skip_build:
        for mode in ('O0','O2'):
            library=(args.libraries or output)/f'libcadkernel-{mode}.rlib'
            if args.libraries is None:
                compile_program([args.rustc,'--edition=2021','--crate-name=cadkernel',source/'src/lib.rs','--cfg','feature="geom2d"','--cfg','feature="brep"','-L',f'dependency={deps}',*externs,'-C',f'opt-level={mode[1]}','--crate-type=rlib','-o',library])
            compile_program([args.rustc,'--edition=2021',ROOT/'tests/cadkernel/brep_make_faceted/reference.rs','--extern',f'cadkernel={library}','-L',f'dependency={deps}','-C',f'opt-level={mode[1]}','-o',output/f'reference-{mode}.out'])
            print(COMMON['verify_fixture'](ROOT/'tests/cadkernel/brep_make_faceted',mode,args.compiler,output,60,30,False),flush=True)
            status,text,elapsed=COMMON['run_process']([str(args.compiler),str(ROOT/'tests/cadkernel/brep_make_faceted/probe.dl'),f'-{mode}','-o',str(output/f'probe-{mode}.out')],timeout=60,cwd=ROOT,phase='compilation')
            (output/f'probe-{mode}-compile.txt').write_text(text)
            assert status==0 and not text.strip(),text
            print(f'probe-{mode}: compile={elapsed:.3f}s',flush=True)
    verify_refusals(args.compiler,output)
    count=fields=accepted=0
    for name,points,rings in inputs():
        if args.case not in name:continue
        arguments=[str(x) for x in encode(points,rings)]
        (output/'current-input.json').write_text(json.dumps(dict(name=name,points=points,faces=rings)))
        outputs=[];references=[]
        for mode in ('O0','O2'):
            _,reference,_=run([output/f'reference-{mode}.out',*arguments],timeout=30)
            expected,limits=MEASURE['expected_values'](reference)
            _,text,_=run([output/f'probe-{mode}.out',*arguments],timeout=30)
            actual=CURVE['parsed'](text)
            (output/f'current-{mode}.txt').write_text(text)
            (output/f'current-rust-{mode}.txt').write_text(reference)
            assert len(actual)==len(expected),f'{name}/{mode}: {len(actual)} fields != {len(expected)}'
            for i,(a,b,limit) in enumerate(zip(actual,expected,limits)):
                assert CURVE['equal'](a,b,exact=limit<0,absolute=max(limit,0.)),f'{name}/{mode} field {i}: {a} != Rust {b}'
            outputs.append(actual);references.append(expected)
        for language,pair in (('native',outputs),('Rust',references)):
            assert len(pair[0])==len(pair[1]) and all(CURVE['equal'](a,b,exact=True) for a,b in zip(*pair)),f'{name}: {language} optimization divergence'
        count+=1;fields+=len(expected)*2;accepted+=int(expected[0])
        if count%25==0:print(f'{count} cases passed ({name})',flush=True)
    assert count and hashlib.sha256(args.compiler.read_bytes()).hexdigest()==compiler_hash
    report=dict(revision=CURVE['PIN'],compiler_sha256=compiler_hash,rustc=version,full_module_verified=False,operation='faceted_solid',source_groups=2,cases=count,accepted=accepted,fields=fields,case_filter=args.case,exact_native_optimization_parity=True,exact_rust_optimization_parity=True,canonicalization='Edges and their associated curves are ordered by vertex pair; coedge edge references use that pair. All other arena order and all geometry fields remain unchanged.')
    report['source_sha256']={p:hashlib.sha256((source/p).read_bytes()).hexdigest() for p in paths}
    report['native_source_sha256']={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [ROOT/'lib/cadkernel/brep_make.dl',ROOT/'tests/cadkernel/brep_make/emit.dl',*sorted((ROOT/'tests/cadkernel/brep_make_faceted').glob('*.dl'))]}
    report['reference_library_sha256']={mode:hashlib.sha256(((args.libraries or output)/f'libcadkernel-{mode}.rlib').read_bytes()).hexdigest() for mode in ('O0','O2')}
    (output/('filtered-summary.json' if args.case else 'summary.json')).write_text(json.dumps(report,indent=2)+'\n')
    print(f'PASS: {count} cases, {fields} Rust comparisons, {accepted} accepted; artifacts={output}',flush=True)

if __name__=='__main__':main()
