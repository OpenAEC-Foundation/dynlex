"""The local preview helper serves its build without opening desktop windows."""
from pathlib import Path
import os
import socket
import subprocess
import tempfile
import time
import unittest
from urllib.request import urlopen
from urllib.error import URLError

ROOT = Path(__file__).resolve().parents[2]


class SitePreview(unittest.TestCase):
    def test_preview_serves_the_selected_build_without_starting_a_browser(self):
        script = ROOT / 'scripts/view_web.sh'
        source = script.read_text()
        for launcher in ['xdg-open', 'wslview', 'powershell.exe', 'open_browser']:
            self.assertNotIn(launcher, source)
        self.assertIn('build/site', source)
        with tempfile.TemporaryDirectory() as directory, socket.socket() as reservation:
            build = Path(directory) / 'selected-build'
            build.mkdir()
            (build / 'index.html').write_text('Selected DynLex build')
            reservation.bind(('127.0.0.1', 0))
            port = reservation.getsockname()[1]
            reservation.close()
            with (build / 'server.log').open('w+') as log:
                process = subprocess.Popen([os.environ.get('DYNLEX_TEST_BASH', 'bash'), str(script), str(port)], cwd=directory,
                    env=dict(os.environ, DYNLEX_WEB_ROOT='selected-build', DYNLEX_WEB_HOST='127.0.0.1'),
                    stdout=log, stderr=subprocess.STDOUT)
                try:
                    for _ in range(50):
                        if process.poll() is not None:
                            log.seek(0)
                            self.fail(log.read())
                        try:
                            with urlopen(f'http://127.0.0.1:{port}/', timeout=1) as response:
                                self.assertEqual(response.read(), b'Selected DynLex build')
                                self.assertEqual(response.headers['Cache-Control'], 'no-store')
                            break
                        except URLError as error:
                            if not isinstance(error.reason, ConnectionRefusedError):
                                raise
                            time.sleep(0.05)
                    else:
                        self.fail('The preview server did not start')
                finally:
                    if process.poll() is None:
                        if os.name == 'nt':
                            subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'],
                                check=True, capture_output=True)
                        else:
                            process.terminate()
                    process.wait(timeout=5)


if __name__ == '__main__':
    unittest.main()
