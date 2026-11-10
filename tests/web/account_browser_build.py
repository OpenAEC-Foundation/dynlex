#!/usr/bin/env python3
"""Prepare a test-only configured site from a completed normal site build."""
import json
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from build_site import compile_jobs
from source_package import SourcePackage

base = Path(sys.argv[1]).resolve()
destination = Path(sys.argv[2]).resolve()
if base == destination or base in destination.parents or destination in base.parents:
    raise ValueError('The source build and test destination must be separate directories')
if destination.exists():
    shutil.rmtree(destination)
shutil.copytree(base, destination)
project = json.loads((ROOT / 'site/project.json').read_text())
sdk = SourcePackage(ROOT / 'site' / project['dependency']['archive'])
sources = {path.relative_to(ROOT).as_posix(): path.read_text() for path in (ROOT / 'site').rglob('*.dl')}
sources.update(sdk.source_files('site/packages/dynlex-ui'))
sources['site/account/configuration.dl'] = '''import lib/json_builders.dl
to get the website account configuration:
    return a JSON object containing [JSON text "apiKey" is "demo-key", JSON text "projectId" is "demo-dynlex-games", JSON text "authDomain" is "demo-dynlex-games.firebaseapp.com", JSON text "appId" is "demo-app"]
'''
compile_jobs(ROOT, sources, [{'entry': project['entry'], 'kind': 'wasm', 'output': 'application.wasm'}], destination)
shutil.copyfile(ROOT / 'tests/web/fixtures/firebase-browser.mjs', destination / 'platform/firebase-test.js')
start = destination / 'platform/start.js'
start.write_text(start.read_text().replace("from './firebase.js'", "from './firebase-test.js'"))
print('Prepared private-emulator test build:', destination)
