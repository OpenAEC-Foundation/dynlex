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

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',type=Path,default=ROOT/'build/dynlex')
    args=parser.parse_args()
    sdk=SourcePackage(ROOT/'site/dependencies/dynlex-ui.zip')
    with tempfile.TemporaryDirectory(prefix='dynlex-farm-art-') as folder:
        project=Path(folder)
        sources={}
        for path in [ROOT/'site/games/farm/art.dl',*(ROOT/'site/games/farm/art').glob('*.dl'),ROOT/'tests/web/farm_art.dl']:
            name=path.relative_to(ROOT).as_posix()
            destination=project/name
            destination.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(path,destination)
            sources[name]=path.read_text()
        sdk.extract(project/'site/packages/dynlex-ui')
        sources.update(sdk.source_files('site/packages/dynlex-ui'))
        shutil.copyfile(ROOT/'tests/web/farm_art_cases.json',project/'input.json')
        entry='tests/web/farm_art.dl'
        compiler=args.compiler.resolve()
        for optimization in ['-O0','-O2']:
            binary=project/'art.out'
            compiled=subprocess.run([str(compiler),entry,optimization,'-o',str(binary)],cwd=project,check=True,timeout=90,capture_output=True,text=True)
            if compiled.stderr:
                raise AssertionError(compiled.stderr)
            snapshot=project/'native.json'
            with snapshot.open('w') as stream:
                subprocess.run([str(binary)],cwd=project,stdout=stream,check=True,timeout=30)
            subprocess.run(['node',str(ROOT/'tests/web/farm_art.mjs'),str(snapshot)],cwd=ROOT,check=True,timeout=30)
            output=project/'native.wasm'
            compiled=subprocess.run([str(compiler),entry,'--emit-wasm',optimization,'-o',str(output)],cwd=project,check=True,timeout=90,capture_output=True,text=True)
            if compiled.stderr:
                raise AssertionError(compiled.stderr)
            subprocess.run(['node',str(ROOT/'tests/web/farm_art.mjs'),str(output)],cwd=ROOT,check=True,timeout=30)
            print('PASS Farm model source',optimization,flush=True)
        request=project/'request.json'
        request.write_text(json.dumps({'sources':sources,'jobs':[{'entry':entry,'kind':'wasm','output':'browser.wasm'}]}))
        subprocess.run(['node',str(ROOT/'scripts/compile_project.mjs'),str(request),str(project)],cwd=ROOT,check=True,timeout=120)
        subprocess.run(['node',str(ROOT/'tests/web/farm_art.mjs'),str(project/'browser.wasm')],cwd=ROOT,check=True,timeout=30)

if __name__=='__main__':
    main()
