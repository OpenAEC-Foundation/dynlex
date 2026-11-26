#!/usr/bin/env python3
"""Export the browser compiler, runtime and build tools as a relocatable package."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def package_files(root):
    files = {}
    directories = ['lib', 'packages', 'src/web/compiler', 'src/web/ide/public/compiler', 'resources/branding']
    for directory in directories:
        for source in sorted((root / directory).rglob('*')):
            if source.is_file() and source.suffix in ['.dl', '.js', '.mjs', '.wasm', '.svg'] and '/tests/' not in source.as_posix():
                files[source.relative_to(root).as_posix()] = source.read_bytes()
    for name in ['scripts/compile_project.mjs', 'scripts/run_program.mjs', 'scripts/test_web_browser.sh', 'scripts/install_release.sh',
                 'web/wgsl-translator.js', 'web/graphics-backend.js', 'LICENSE.md', 'shared/call-colors.json', 'metadata/release-manifest.txt',
                 'tests/web/allocation_probe.mjs', 'tests/web/json_memory.mjs', 'tests/web/browser_test_driver.mjs', 'tests/web/float_shader_reference.mjs']:
        files[name] = (root / name).read_bytes()
    files['package.json'] = b'{"type":"module","private":true}\n'
    for name in ['dynlex_web.js', 'dynlex_web.wasm', 'dynlex_wgsl_translator.wasm', 'runtimeImports.js']:
        if f'src/web/ide/public/compiler/{name}' not in files:
            raise FileNotFoundError(f'Build the browser compiler first: {name}')
    return files


def export(root, output, revision):
    files = package_files(root)
    digests = {name: hashlib.sha256(data).hexdigest() for name, data in sorted(files.items())}
    content_hash = hashlib.sha256(json.dumps(digests, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
    manifest = {'schema': 1, 'package': 'dynlex-web', 'sourceRevision': revision,
                'contentHash': content_hash, 'files': digests}
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted({**files, 'sdk.json': (json.dumps(manifest, indent=2) + '\n').encode()}.items()):
            item = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
            item.compress_type = zipfile.ZIP_DEFLATED
            item.external_attr = (0o100755 if name.endswith('.sh') else 0o100644) << 16
            archive.writestr(item, data, compresslevel=9)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    manifest = export(ROOT, args.output.resolve(), revision)
    print(json.dumps({key: manifest[key] for key in ['package', 'sourceRevision', 'contentHash']}))


if __name__ == '__main__':
    main()
