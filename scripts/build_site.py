#!/usr/bin/env python3
"""Build the DynLex website from its page and shader descriptions and a pinned UI SDK."""
import argparse
import copy
import html
import json
import posixpath
from pathlib import Path
import shutil
import subprocess
import tempfile

from source_package import SourcePackage


def compile_jobs(root, sources, jobs, stage):
    request = stage / 'compile-request.json'
    request.write_text(json.dumps({'sources': sources, 'jobs': jobs}))
    subprocess.run(['node', str(root / 'scripts/compile_project.mjs'), str(request), str(stage),
                    str(root / 'src/web/ide/public/compiler')], check=True)
    request.unlink()


def run_json(root, program):
    result = subprocess.run(['node', str(root / 'scripts/run_program.mjs'), str(program)],
                            check=True, text=True, capture_output=True)
    return json.loads(result.stdout)


def link_shader_program(description, directory):
    """Resolve stage artifacts and their reflected bindings, independent of scene behavior."""
    program = copy.deepcopy(description)
    for render_pass in program['passes']:
        uniforms = {}
        for stage in ['fragment', 'vertex']:
            prefix = render_pass[stage]
            if prefix is None:
                continue
            reflection = json.loads((directory / (prefix + '.json')).read_text())
            for uniform in reflection['uniforms']:
                key = (uniform['group'], uniform['binding'])
                entry = {name: uniform[name] for name in ['name', 'group', 'binding']}
                if key in uniforms and uniforms[key] != entry:
                    raise ValueError(f'Shader stages disagree about uniform binding {key}')
                uniforms[key] = entry
            render_pass[stage] = {
                backend: {'kind': 'url', 'value': prefix + suffix}
                for backend, suffix in [('webgpu', '.wgsl'), ('webgl', '.glsl')]
            }
        render_pass['uniforms'] = [uniforms[key] for key in sorted(uniforms)]
    return program


def write_routes(site, stage, pages):
    template = (site / 'shell.html').read_text()
    for page in pages:
        route = page['path'].removeprefix('/')
        target = stage / route
        target.parent.mkdir(parents=True, exist_ok=True)
        source = template.replace('DynLex — Make the language part of the work', html.escape(page['title']))
        source = source.replace('Explore working sketches, invent project vocabulary, and run DynLex in the browser.',
                                html.escape(page['description'], quote=True))
        for asset in ['platform/start.js', 'assets/branding/dynlex-icon.svg']:
            source = source.replace('./' + asset, posixpath.relpath(asset, posixpath.dirname(route) or '.'))
        target.write_text(source)
    (stage / 'routes.json').write_text(json.dumps(pages, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('build/site'))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    site = root / 'site'
    output = args.output.resolve()
    project = json.loads((site / 'project.json').read_text())
    sdk = SourcePackage(site / project['dependency']['archive'])
    if sdk.manifest['contentHash'] != project['dependency']['contentHash']:
        raise ValueError('The UI SDK does not match the project dependency lock')
    sources = {path.relative_to(root).as_posix(): path.read_text() for path in site.rglob('*.dl')}
    sources.update(sdk.source_files('site/packages/dynlex-ui'))
    (root / 'build').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='dynlex-site-build-', dir=root / 'build') as temporary:
        stage = Path(temporary)
        compile_jobs(root, sources, [
            {'entry': 'site/shaders/export.dl', 'kind': 'wasm', 'output': 'shader-catalog.wasm'},
            {'entry': 'site/pages/export.dl', 'kind': 'wasm', 'output': 'page-catalog.wasm'}
        ], stage)
        scenes = run_json(root, stage / 'shader-catalog.wasm')
        pages = run_json(root, stage / 'page-catalog.wasm')
        (stage / 'shader-catalog.wasm').unlink()
        (stage / 'page-catalog.wasm').unlink()
        jobs = [{'entry': project['entry'], 'kind': 'wasm', 'output': 'application.wasm'}]
        for scene in scenes:
            stages = {'fragment'}
            if any(render_pass['vertex'] is not None for render_pass in scene['program']['passes']):
                stages.add('vertex')
            for shader_stage in sorted(stages):
                jobs.append({'entry': scene['source'], 'kind': 'shader', 'stage': shader_stage,
                             'output': f"assets/shaders/{scene['id']}.{shader_stage}"})
        compile_jobs(root, sources, jobs, stage)
        for scene in scenes:
            scene['program'] = link_shader_program(scene['program'], stage)
        sdk.extract(stage / 'packages/dynlex-ui')
        shutil.copytree(site / 'assets', stage / 'assets', dirs_exist_ok=True)
        shutil.copytree(site / 'platform', stage / 'platform')
        shutil.copytree(root / 'src/web/compiler', stage / 'compiler')
        compiler = root / 'src/web/ide/public/compiler'
        for name in ['dynlex_web.js', 'dynlex_web.wasm', 'dynlex_wgsl_translator.wasm',
                     'runtimeImports.js', 'runtimeFilesystem.js', 'runtimeHost.js', 'runtimeLayout.js']:
            shutil.copy2(compiler / name, stage / 'compiler' / name)
        shutil.copy2(root / 'web/wgsl-translator.js', stage / 'compiler/wgsl-translator.js')
        write_routes(site, stage, pages)
        (stage / 'assets/shaders/index.json').write_text(json.dumps(scenes) + '\n')
        (stage / 'sdk-sources.json').write_text(json.dumps(sdk.source_files('packages/dynlex-ui')) + '\n')
        fonts = sorted(path.relative_to(stage).as_posix() for path in (stage / 'assets/fonts').glob('*.ttf'))
        fonts += sorted(path.relative_to(stage).as_posix() for path in (stage / 'packages/dynlex-ui/assets/fonts').glob('*.ttf'))
        (stage / 'application.json').write_text(json.dumps({'module': 'application.wasm', 'fonts': fonts}, indent=2) + '\n')
        if output.exists():
            shutil.rmtree(output)
        output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(stage, output)
    print(f'Built DynLex site: {output}')


if __name__ == '__main__':
    main()
