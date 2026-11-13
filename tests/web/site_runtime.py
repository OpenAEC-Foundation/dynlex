#!/usr/bin/env python3
"""Verify website models and controllers against the pinned UI source package."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from source_package import SourcePackage


def compilation_budget(name):
    # The production dispatcher links both the source workspace and 3D scene.
    return 600 if name in ['site_farm_controller', 'site_farm_profiles'] else 180


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, default=ROOT / 'build/dynlex')
    cases = ['site_controllers', 'site_language', 'site_entry', 'site_shader', 'site_shader_buffers', 'site_program_events', 'site_river_controller', 'site_river_audio', 'site_progress_controller', 'site_account_profiles', 'site_account_controller', 'site_account_recovery', 'site_reply_ownership']
    cases.append('site_program_commands')
    cases.append('site_farm_controller')
    cases.append('site_farm_profiles')
    renderers = ['site_farm_renderer']
    browser_cases = renderers + ['site_farm_controller', 'site_farm_profiles']
    cases.extend(renderers)
    models = ['site_shader_program', 'site_farm_interface', 'site_farm_landscape', 'site_farm_camera', 'site_farm_lighting', 'site_farm_materials', 'site_farm_characters', 'site_farm_fences', 'site_farm_flags', 'site_farm_areas']
    models.append('site_farm_picking')
    parser.add_argument('--case', action='append', choices=cases + models)
    args = parser.parse_args()
    package = SourcePackage(ROOT / 'site/dependencies/dynlex-ui.zip')
    with tempfile.TemporaryDirectory(prefix='dynlex-site-test-') as directory:
        project = Path(directory)
        for source in (ROOT / 'site').rglob('*.dl'):
            target = project / source.relative_to(ROOT)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
        package.extract(project / 'site/packages/dynlex-ui')
        fixture = project / 'tests/web/site_fixture.dl'
        fixture.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'tests/web/site_fixture.dl', fixture)
        shutil.copyfile(ROOT / 'tests/web/site_account_support.dl', fixture.with_name('site_account_support.dl'))
        for name in args.case or cases + models:
            source = project / f'tests/web/{name}.dl'
            source.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / f'tests/web/{name}.dl', source)
        for name in args.case or cases + models:
            source = project / f'tests/web/{name}.dl'
            if name in models:
                expected = (ROOT / f'tests/web/models/{name}.txt').read_text()
                for suffix, options in [('.out', ['-O0']), ('.optimized.out', ['-O2']), ('.wasm', ['--emit-wasm'])]:
                    binary = project / (name + suffix)
                    subprocess.run([str(args.compiler.resolve()), str(source), *options, '-o', str(binary)],
                                   cwd=ROOT, check=True, timeout=180)
                    if suffix == '.wasm':
                        subprocess.run(['node', str(ROOT / 'tests/web/site_model_runtime.mjs'), str(binary),
                                        str(ROOT / f'tests/web/models/{name}.txt')], cwd=ROOT, check=True, timeout=30)
                        print(f'PASS {name}{suffix}', flush=True)
                        continue
                    command = [str(binary)]
                    result = subprocess.run(command, cwd=ROOT, timeout=30, text=True, capture_output=True)
                    if result.returncode:
                        raise AssertionError(f'{name}{suffix}: exit {result.returncode}; stdout={result.stdout!r}; stderr={result.stderr!r}')
                    if result.stdout != expected:
                        raise AssertionError(f'{name}{suffix}: expected {expected!r}, got {result.stdout!r}')
                    print(f'PASS {name}{suffix}', flush=True)
                continue
            binary = project / f'{name}.wasm'
            optimization = '-O0' if name in renderers else '-O2'
            subprocess.run([str(args.compiler.resolve()), str(source), '--emit-wasm', optimization, '-o', str(binary)],
                           cwd=ROOT, check=True, timeout=compilation_budget(name))
            subprocess.run(['node', str(ROOT / f'tests/web/{name}.mjs'), str(binary)],
                           cwd=ROOT, check=True, timeout=30)
            if name in renderers:
                binary = project / f'{name}.optimized.wasm'
                subprocess.run([str(args.compiler.resolve()), str(source), '--emit-wasm', '-O2', '-o', str(binary)],
                               cwd=ROOT, check=True, timeout=180)
                subprocess.run(['node', str(ROOT / f'tests/web/{name}.mjs'), str(binary)],
                               cwd=ROOT, check=True, timeout=30)
        jobs = [{'entry': f'tests/web/{name}.dl', 'kind': 'wasm', 'output': f'{name}.browser.wasm'}
                for name in args.case or cases + models if name in models + browser_cases]
        if jobs:
            sources = {source.relative_to(project).as_posix(): source.read_text()
                       for source in project.rglob('*.dl')}
            request = project / 'browser-models.json'
            request.write_text(json.dumps({'sources': sources, 'jobs': jobs}))
            subprocess.run(['node', str(ROOT / 'scripts/compile_project.mjs'), str(request), str(project)],
                           cwd=ROOT, check=True, timeout=sum(compilation_budget(Path(job['entry']).stem) for job in jobs))
            for job in jobs:
                name = Path(job['entry']).stem
                command = ['node', str(ROOT / 'tests/web/site_model_runtime.mjs'),
                           str(project / job['output']), str(ROOT / f'tests/web/models/{name}.txt')] if name in models else [
                               'node', str(ROOT / f'tests/web/{name}.mjs'), str(project / job['output'])]
                subprocess.run(command,
                               cwd=ROOT, check=True, timeout=30)
                print(f'PASS {job["output"]}', flush=True)


if __name__ == '__main__':
    main()
