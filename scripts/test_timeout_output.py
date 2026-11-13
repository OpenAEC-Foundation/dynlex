#!/usr/bin/env python3
"""The watchdog must preserve the child's combined output order."""

import os
from pathlib import Path
import subprocess
import sys


def main():
    project = Path(__file__).resolve().parents[1]
    source = (project / "scripts" / "test.sh").read_text(encoding="utf-8")
    wrapper = "run_with_timeout() {" + source.split("run_with_timeout() {", 1)[1].split('\nif [[ "$run_only"', 1)[0]
    shell = os.environ.get("DYNLEX_TEST_BASH", "bash")
    script = 'PROJECT_DIR=$1\n' + wrapper + '\nrun_with_timeout "$2" "$3" -c "$4" 2>&1\n'
    writes = ('import os,time\n'
              'os.write(1, b"first out\\n")\n'
              'os.write(2, b"first error\\n")\n'
              'os.write(1, b"second out\\n")\n'
              'os.write(2, b"second error\\n")\n')
    expected = "first out\nfirst error\nsecond out\nsecond error\n"
    for unbuffered in (False, True):
        environment = os.environ.copy()
        environment.pop("PYTHONUNBUFFERED", None)
        if unbuffered:
            environment["PYTHONUNBUFFERED"] = "1"
        for timeout in (False, True):
            child = writes + ("time.sleep(60)\n" if timeout else "")
            seconds = ("2" if os.name == "nt" else "1") if timeout else "5"
            result = subprocess.run(
                [shell, "-c", script, "timeout-output", project.as_posix(), seconds, sys.executable, child],
                capture_output=True, text=True, env=environment, timeout=10,
            )
            assert result.returncode == (124 if timeout else 0), (result.returncode, result.stdout, result.stderr)
            assert result.stdout == expected, (unbuffered, timeout, result.stdout)
            assert not result.stderr, result.stderr


if __name__ == "__main__":
    main()
