#!/usr/bin/env python3
"""Check the browser distribution's ownership, integrity and reproducibility."""
import hashlib
import json
from pathlib import Path
import tempfile
import zipfile
from package_web_compiler import export, ROOT

with tempfile.TemporaryDirectory() as temporary:
    directory = Path(temporary)
    first, second = directory / 'first.zip', directory / 'second.zip'
    manifest = export(ROOT, first, 'test-revision')
    export(ROOT, second, 'test-revision')
    assert first.read_bytes() == second.read_bytes(), 'Package export must be reproducible'
    with zipfile.ZipFile(first) as archive:
        assert set(archive.namelist()) == set(manifest['files']) | {'sdk.json'}
        for name, digest in manifest['files'].items():
            assert hashlib.sha256(archive.read(name)).hexdigest() == digest, name
        assert all(not name.startswith('site/') for name in archive.namelist())
        assert 'lib/farm_challenge.dl' not in archive.namelist()
        assert 'lib/river_challenge.dl' not in archive.namelist()
        for name in ['scripts/compile_project.mjs', 'scripts/install_release.sh',
                     'packages/manager.mjs', 'packages/browser.mjs', 'packages/archive.mjs',
                     'src/web/compiler/services.js', 'resources/branding/dynlex-icon.svg',
                     'src/web/ide/public/compiler/runtimeHeap.js',
                     'shared/call-colors.json', 'metadata/release-manifest.txt']:
            assert name in archive.namelist(), name
print('Browser compiler package: reproducible, complete, integrity-checked and application-independent.')
