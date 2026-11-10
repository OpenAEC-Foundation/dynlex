"""Route shells share the selected immutable asset base, including nested pages."""
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from build_site import asset_base, write_routes


class SiteRoutes(unittest.TestCase):
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
