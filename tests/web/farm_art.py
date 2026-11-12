#!/usr/bin/env python3
"""Verify authored Farm models on native, Wasm and the shipped browser compiler."""
import argparse
from pathlib import Path
import json
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from source_package import SourcePackage

def compile_fixture(command,project):
    compiled=subprocess.run(command,cwd=project,timeout=90,capture_output=True,text=True)
    if compiled.returncode or compiled.stderr:
        raise RuntimeError(f'Fixture compilation exited {compiled.returncode}:\n{compiled.stdout}{compiled.stderr}')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',type=Path,default=ROOT/'build/dynlex')
    parser.add_argument('--case',choices=['models','crops'],default='models')
    args=parser.parse_args()
    fixture='farm_art' if args.case=='models' else 'farm_crops'
    sdk=SourcePackage(ROOT/'site/dependencies/dynlex-ui.zip')
    with tempfile.TemporaryDirectory(prefix='dynlex-farm-art-') as folder:
        project=Path(folder)
        sources={}
        for path in [ROOT/'site/games/farm/art.dl',*(ROOT/'site/games/farm/art').glob('*.dl'),ROOT/f'tests/web/{fixture}.dl']:
            name=path.relative_to(ROOT).as_posix()
            destination=project/name
            destination.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(path,destination)
            sources[name]=path.read_text()
        sdk.extract(project/'site/packages/dynlex-ui')
        sources.update(sdk.source_files('site/packages/dynlex-ui'))
        if args.case=='models':
            shutil.copyfile(ROOT/'tests/web/farm_art_cases.json',project/'input.json')
        else:
            cases=json.loads((ROOT/'tests/web/models/farm_crops/manifest.json').read_text())['cases']
            (project/'input.json').write_text(json.dumps(cases))
        entry=f'tests/web/{fixture}.dl'
        compiler=args.compiler.resolve()
        for optimization in ['-O0','-O2']:
            binary=project/'art.out'
            compile_fixture([str(compiler),entry,optimization,'-o',str(binary)],project)
            snapshot=project/'native.json'
            with snapshot.open('w') as stream:
                subprocess.run([str(binary)],cwd=project,stdout=stream,check=True,timeout=30)
            subprocess.run(['node',str(ROOT/f'tests/web/{fixture}.mjs'),str(snapshot)],cwd=ROOT,check=True,timeout=60)
            output=project/'native.wasm'
            compile_fixture([str(compiler),entry,'--emit-wasm',optimization,'-o',str(output)],project)
            subprocess.run(['node',str(ROOT/f'tests/web/{fixture}.mjs'),str(output)],cwd=ROOT,check=True,timeout=60)
            print('PASS Farm model source',optimization,flush=True)
        request=project/'request.json'
        request.write_text(json.dumps({'sources':sources,'jobs':[{'entry':entry,'kind':'wasm','output':'browser.wasm'}]}))
        subprocess.run(['node',str(ROOT/'scripts/compile_project.mjs'),str(request),str(project)],cwd=ROOT,check=True,timeout=120)
        subprocess.run(['node',str(ROOT/f'tests/web/{fixture}.mjs'),str(project/'browser.wasm')],cwd=ROOT,check=True,timeout=60)

if __name__=='__main__':
    main()
