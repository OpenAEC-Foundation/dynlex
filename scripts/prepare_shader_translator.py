#!/usr/bin/env python3
"""Prepare the pinned, patched Naga source used by native and Wasm translator builds."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
from urllib.request import urlopen


def digest(data):
    return hashlib.sha256(data).hexdigest()


def source_files(source):
    return {path.relative_to(source).as_posix(): digest(path.read_bytes())
            for path in sorted(source.rglob('*')) if path.is_file()}


def prepare(root, check=False):
    metadata = root / 'metadata/SHADER_TRANSLATOR'
    specification = json.loads(metadata.read_text())
    patch = root / specification['patch']
    producer = Path(__file__).read_bytes()
    identity = digest(metadata.read_bytes() + b'\0' + patch.read_bytes() + b'\0' + producer)
    cache = root / '.cache/shader-translator'
    source, receipt = cache / 'naga', cache / 'source.json'
    if source.exists():
        recorded = json.loads(receipt.read_text())
        if source_files(source) != recorded['files']:
            raise RuntimeError('Prepared Naga source differs from its receipt; remove .cache/shader-translator before preparing it again')
        if recorded['identity'] == identity:
            print(f'Pinned shader translator source verified: {source}')
            return source
    if check:
        raise RuntimeError('Pinned shader translator source has not been prepared for these inputs')
    for command in ['patch']:
        if shutil.which(command) is None:
            raise RuntimeError(f'Missing shader translator build dependency: {command}')
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / f'naga-{specification["version"]}.crate'
    if not archive.exists():
        with tempfile.TemporaryDirectory(prefix='download-', dir=cache) as directory:
            temporary = Path(directory) / 'naga.crate'
            with urlopen(specification['archive'], timeout=30) as response, temporary.open('wb') as pending:
                shutil.copyfileobj(response, pending)
            if digest(temporary.read_bytes()) != specification['sha256']:
                raise RuntimeError('Downloaded Naga source does not match its pinned SHA-256')
            temporary.replace(archive)
    if digest(archive.read_bytes()) != specification['sha256']:
        raise RuntimeError('Cached Naga archive does not match its pinned SHA-256')
    with tempfile.TemporaryDirectory(prefix='prepare-', dir=cache) as temporary:
        staging = Path(temporary)
        with tarfile.open(archive) as package:
            package.extractall(staging, filter='data')
        prepared = staging / f'naga-{specification["version"]}'
        subprocess.run(['patch', '--batch', '--fuzz=0', '-p1', '-d', str(prepared), '-i', str(patch)], check=True)
        recorded = {'identity': identity, 'files': source_files(prepared)}
        next_receipt = staging / 'source.json'
        next_receipt.write_text(json.dumps(recorded, indent=2) + '\n')
        previous = staging / 'previous'
        if source.exists():
            source.replace(previous)
        try:
            prepared.replace(source)
        except BaseException:
            if previous.exists():
                previous.replace(source)
            raise
        next_receipt.replace(receipt)
    print(f'Prepared pinned shader translator source: {source}')
    return source


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    arguments = parser.parse_args()
    prepare(Path(__file__).resolve().parents[1], arguments.check)
