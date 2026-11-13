"""Route shells share the selected immutable asset base, including nested pages."""
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from build_site import asset_base, write_routes, copy_public_files


class SiteRoutes(unittest.TestCase):
    def test_deployment_builds_the_dynlex_site_with_immutable_asset_urls(self):
        workflow = (ROOT / '.github/workflows/deploy.yml').read_text()
        self.assertIn('python3 scripts/build_site.py --base "/releases/$RELEASE/" --output "$RUNNER_TEMP/site"', workflow)
        self.assertNotIn('build_web_root.sh', workflow)
        self.assertNotIn('prepare_web_deployment.mjs', workflow)

    def test_public_files_are_published_without_replacing_generated_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            site, stage = root / 'site', root / 'output'
            public = site / 'public'
            public.mkdir(parents=True)
            stage.mkdir()
            installer = public / 'install.sh'
            installer.write_bytes(b'#!/bin/sh\nset -eu\n')
            installer.chmod(0o755)
            (public / 'nested').mkdir()
            (public / 'nested/notice.txt').write_text('Public notice\n')
            (stage / 'application.wasm').write_bytes(b'generated application')
            copy_public_files(site, stage)
            self.assertEqual((stage / 'install.sh').read_bytes(), installer.read_bytes())
            self.assertEqual((stage / 'install.sh').stat().st_mode, installer.stat().st_mode)
            self.assertEqual((stage / 'nested/notice.txt').read_text(), 'Public notice\n')
            (public / 'application.wasm').write_bytes(b'collision')
            with self.assertRaises(FileExistsError):
                copy_public_files(site, stage)
            self.assertEqual((stage / 'application.wasm').read_bytes(), b'generated application')

    def test_every_route_uses_the_selected_asset_base(self):
        pages = [
            {'path': '/index.html', 'title': 'DynLex <home>', 'description': 'Draw "ideas" & run.'},
            {'path': '/wiki/tutorial/index.html', 'title': 'Tutorial', 'description': 'Learn DynLex.'},
        ]
        for base in ['/', '/releases/' + 'a' * 40 + '/', '/preview/build-7/']:
            with self.subTest(base=base), tempfile.TemporaryDirectory() as directory:
                stage = Path(directory)
                write_routes(ROOT / 'site', stage, pages, asset_base(base))
                for page in pages:
                    source = (stage / page['path'].removeprefix('/')).read_text()
                    self.assertIn(f'src="{base}platform/start.js"', source)
                    self.assertIn(f'href="{base}assets/branding/dynlex-icon.svg"', source)
                    self.assertNotIn('{{', source)
                source = (stage / 'index.html').read_text()
                self.assertIn('<title>DynLex &lt;home&gt;</title>', source)
                self.assertIn('Draw &quot;ideas&quot; &amp; run.', source)

    def test_invalid_asset_bases_are_rejected(self):
        for base in ['', 'relative/', '//external.test/', '/missing-slash', '/a/../b/', '/a//b/',
                     '/%2e%2e/', '/a/?query', '/a/#fragment', '/a\\b/', '/a\nb/',
                     '/a\0b/', '/%00/', '/%5c/']:
            with self.subTest(base=base), self.assertRaises(ValueError):
                asset_base(base)


if __name__ == '__main__':
    unittest.main()
