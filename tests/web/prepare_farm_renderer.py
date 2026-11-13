#!/usr/bin/env python3
"""Compile Farm's real DynLex shaders and comparison data for private GPU tests."""
import argparse
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from source_package import SourcePackage


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--browser', action='store_true', help='Also verify both GPU backends on a private display')
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    package = SourcePackage(ROOT / 'site/dependencies/dynlex-ui.zip')
    package.extract(output / 'packages/dynlex-ui')
    sources = {path.relative_to(ROOT).as_posix(): path.read_text()
               for path in (ROOT / 'site').rglob('*.dl')}
    sources.update(package.source_files('site/packages/dynlex-ui'))
    jobs = [
        {'entry': 'site/games/farm/shaders/environment.dl', 'kind': 'shader', 'stage': 'fragment', 'output': 'environment'},
        {'entry': 'tests/web/farm_lighting_query.dl', 'kind': 'shader', 'stage': 'fragment', 'output': 'query'},
        {'entry': 'site/games/farm/shaders/vertex.dl', 'kind': 'shader', 'stage': 'vertex', 'output': 'surface-vertex'},
        {'entry': 'site/games/farm/shaders/surface.dl', 'kind': 'shader', 'stage': 'fragment', 'output': 'surface-fragment'},
        {'entry': 'site/games/farm/shaders/shadow.dl', 'kind': 'shader', 'stage': 'vertex', 'output': 'shadow'},
        {'entry': 'site/games/farm/shaders/post.dl', 'kind': 'shader', 'stage': 'fragment', 'output': 'post'},
        {'entry': 'site/packages/dynlex-ui/ui/shaders/environment_dfg.dl', 'kind': 'shader', 'stage': 'fragment', 'output': 'scattering'},
        {'entry': 'tests/web/site_farm_lighting_data.dl', 'kind': 'wasm', 'output': 'lighting-data.wasm'},
        {'entry': 'tests/web/site_farm_renderer_data.dl', 'kind': 'wasm', 'output': 'scene-data.wasm'}
    ]
    for entry in ['tests/web/farm_lighting_query.dl', 'tests/web/site_farm_lighting_data.dl', 'tests/web/site_farm_renderer_data.dl']:
        sources[entry] = (ROOT / entry).read_text()
    request = output / 'request.json'
    request.write_text(json.dumps({'sources': sources, 'jobs': jobs}))
    subprocess.run(['node', str(ROOT / 'scripts/compile_project.mjs'), str(request), str(output)],
                   cwd=ROOT, check=True, timeout=180)
    stages = {}
    for name in [job['output'] for job in jobs if job['kind'] == 'shader']:
        stages[name] = {
            'webgpu': (output / f'{name}.wgsl').read_text(),
            'webgl': (output / f'{name}.glsl').read_text(),
            'reflection': json.loads((output / f'{name}.json').read_text())
        }
    (output / 'stages.json').write_text(json.dumps(stages))
    shutil.copyfile(ROOT / 'tests/web/models/site_farm_lighting.json', output / 'sky-reference.json')
    shutil.copyfile(ROOT / 'tests/web/models/site_farm_materials.json', output / 'material-reference.json')
    shutil.copyfile(ROOT / 'tests/web/farm_renderer_rendering.mjs', output / 'rendering.mjs')
    shutil.copyfile(ROOT / 'tests/web/farm_scene_rendering.mjs', output / 'scene-rendering.mjs')
    (output / 'index.html').write_text('<!doctype html><meta charset="utf-8"><title>Farm renderer test</title><link rel="icon" href="data:,">')
    subprocess.run(['node', str(ROOT / 'tests/web/farm_renderer_data.mjs'), str(output)], cwd=ROOT, check=True, timeout=60)
    print(f'Prepared actual Farm shaders and reference data: {output}')
    if args.browser:
        with socket.socket() as server, socket.socket() as debug:
            server.bind(('127.0.0.1', 0))
            debug.bind(('127.0.0.1', 0))
            ports = (server.getsockname()[1], debug.getsockname()[1])
        environment = dict(os.environ, DYNLEX_TEST_WEB_SERVER='static',
                           DYNLEX_TEST_WEB_ROOT=str(output), DYNLEX_TEST_GRAPHICS='webgpu',
                           DYNLEX_TEST_SERVER_PORT=str(ports[0]), DYNLEX_TEST_DEBUG_PORT=str(ports[1]),
                           DYNLEX_BROWSER_TEST_ENTRY=str(ROOT / 'tests/web/farm_renderer_browser.mjs'))
        subprocess.run([str(ROOT / 'scripts/test_web_browser.sh')], cwd=ROOT, env=environment, check=True)


if __name__ == '__main__':
    main()
