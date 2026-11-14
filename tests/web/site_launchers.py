"""Check the browser launch contract without starting a browser or compiler."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class BrowserLaunchers(unittest.TestCase):
    def run_launcher(self, **settings):
        with tempfile.TemporaryDirectory(prefix='dynlex-launchers-') as directory:
            temporary = Path(directory)
            binaries = temporary / 'bin'
            binaries.mkdir()
            site = temporary / 'site with spaces'
            site.mkdir()
            (site / 'index.html').write_text('<!doctype html>')
            capture = temporary / 'calls.jsonl'
            for name in ['node', 'google-chrome', 'Xvfb']:
                executable = binaries / name
                executable.write_text('#!/bin/sh\nexit 0\n')
                executable.chmod(0o755)
            executable = binaries / 'xvfb-run'
            executable.write_text(f'#!{sys.executable}\n' + '''import json,os,sys
with open(os.environ['DYNLEX_LAUNCH_CAPTURE'],'a') as output:
    output.write(json.dumps({'args':sys.argv[1:], 'scenario':os.environ['DYNLEX_SITE_SCENARIO'],
        'background':os.environ['DYNLEX_BACKGROUND_BROWSER'], 'session':os.environ['XDG_SESSION_TYPE'],
        'wayland':os.environ.get('WAYLAND_DISPLAY')})+'\\n')
''')
            executable.chmod(0o755)
            environment = {key: value for key, value in os.environ.items()
                           if not key.startswith(('DYNLEX_TEST_', 'DYNLEX_SITE_'))
                           and key != 'DYNLEX_BROWSER_TEST_ENTRY'}
            environment.update(PATH=str(binaries) + os.pathsep + os.environ['PATH'],
                               DYNLEX_TEST_WEB_ROOT=str(site),
                               DYNLEX_LAUNCH_CAPTURE=str(capture), WAYLAND_DISPLAY='user-desktop')
            environment.update(settings)
            result = subprocess.run(['bash', str(ROOT / 'scripts/test_web_browser.sh')],
                                    env=environment, cwd=temporary, capture_output=True, text=True)
            calls = [json.loads(line) for line in capture.read_text().splitlines()] if capture.exists() else []
            return result, calls

    def test_default_runs_all_scenarios_and_backends_in_private_displays(self):
        result, calls = self.run_launcher()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(calls), 12)
        actual = set()
        for call in calls:
            self.assertEqual(call['background'], '1')
            self.assertEqual(call['session'], 'x11')
            self.assertIsNone(call['wayland'])
            self.assertEqual(call['args'][:4], ['-a', '-s', '-screen 0 1400x1000x24', 'node'])
            expected = 'site_river_browser.mjs' if call['scenario'] == 'river' else 'site_browser.mjs'
            self.assertEqual(Path(call['args'][4]).name, expected)
            self.assertTrue(call['args'][5].endswith('site with spaces'))
            actual.add((call['scenario'], call['args'][6]))
        self.assertEqual(actual, {(scenario, mode) for scenario in
                         ['general', 'farm', 'river-inspection', 'river'] for mode in
                         ['webgpu', 'webgl-no-adapter', 'webgl-no-api']})

    def test_farm_can_be_selected_without_running_other_scenarios(self):
        result, calls = self.run_launcher(DYNLEX_SITE_SCENARIO='farm', DYNLEX_TEST_GRAPHICS='webgl')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(calls), 1)
        self.assertEqual(calls[0]['scenario'], 'farm')
        self.assertEqual(calls[0]['args'][6], 'webgl-no-adapter')

    def test_unknown_scenario_is_rejected_before_launching(self):
        result, calls = self.run_launcher(DYNLEX_SITE_SCENARIO='missing')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Unknown website scenario: missing', result.stdout)
        self.assertEqual(calls, [])


if __name__ == '__main__':
    unittest.main()
