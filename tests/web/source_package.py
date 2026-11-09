import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('source_package', ROOT / 'scripts/source_package.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class SourcePackageTests(unittest.TestCase):
    def package(self, directory, files, manifest_change=None):
        digests = {name: hashlib.sha256(data).hexdigest() for name, data in files.items()}
        manifest = {'schema': 1, 'package': 'example', 'files': digests,
                    'contentHash': hashlib.sha256(json.dumps(digests, sort_keys=True,
                                                            separators=(',', ':')).encode()).hexdigest()}
        if manifest_change:
            manifest_change(manifest)
        archive = Path(directory) / 'test.zip'
        with zipfile.ZipFile(archive, 'w') as output:
            for name, data in files.items():
                output.writestr(name, data)
            output.writestr('sdk.json', json.dumps(manifest))
        return archive

    def test_sources_and_assets_keep_their_namespace(self):
        with tempfile.TemporaryDirectory() as directory:
            package = module.SourcePackage(self.package(directory, {'ui/main.dl': b'code', 'assets/font': b'font'}))
            self.assertEqual(package.source_files('packages/example'), {'packages/example/ui/main.dl': 'code'})
            package.extract(Path(directory) / 'out')
            self.assertEqual((Path(directory) / 'out/assets/font').read_bytes(), b'font')

    def test_corrupt_or_escaping_packages_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            for name in ['../outside.dl', '/absolute', 'a/../b.dl', 'a\\b.dl']:
                with self.assertRaises(ValueError):
                    module.SourcePackage(self.package(directory, {name: b'x'}))
            for change in [lambda m: m['files'].update({'missing': '0' * 64}),
                           lambda m: m.update({'contentHash': '0' * 64}),
                           lambda m: m['files'].update({'main.dl': '0' * 64})]:
                with self.assertRaises(ValueError):
                    module.SourcePackage(self.package(directory, {'main.dl': b'x'}, change))


unittest.main()
