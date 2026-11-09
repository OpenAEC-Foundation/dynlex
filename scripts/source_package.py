"""Read a content-addressed source SDK without trusting archive paths or metadata."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import zipfile


class SourcePackage:
    def __init__(self, archive):
        with zipfile.ZipFile(archive) as package:
            names = package.namelist()
            if len(names) != len(set(names)):
                raise ValueError('Source package contains duplicate files')
            self.manifest = json.loads(package.read('sdk.json'))
            if self.manifest['schema'] != 1:
                raise ValueError('Unsupported source package schema')
            digests = self.manifest['files']
            if set(names) != set(digests) | {'sdk.json'}:
                raise ValueError('Source package manifest does not match its files')
            content_hash = hashlib.sha256(json.dumps(digests, sort_keys=True,
                                                    separators=(',', ':')).encode()).hexdigest()
            if content_hash != self.manifest['contentHash']:
                raise ValueError('Source package manifest hash does not match')
            self.files = {}
            for name, digest in digests.items():
                path = PurePosixPath(name)
                if not name or '\\' in name or path.is_absolute() or '..' in path.parts or path.as_posix() != name:
                    raise ValueError('Source package file must have a normalized relative path')
                data = package.read(name)
                if hashlib.sha256(data).hexdigest() != digest:
                    raise ValueError(f'Source package hash does not match: {name}')
                self.files[name] = data

    def source_files(self, prefix):
        return {f'{prefix}/{name}': data.decode('utf-8')
                for name, data in self.files.items() if name.endswith('.dl')}

    def extract(self, directory):
        directory = Path(directory)
        for name, data in self.files.items():
            target = directory / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
