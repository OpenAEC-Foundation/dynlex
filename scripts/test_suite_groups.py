#!/usr/bin/env python3
"""Independent suite groups overlap and retain every result and worker failure."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    source = Path(__file__).with_name("test.sh").read_text()
    helpers = source.split("start_fixture_tests() {\n", 1)
    assert len(helpers) == 2, "the required fixture group is not started independently"
    helpers = "start_fixture_tests() {\n" + helpers[1].split("\n# Run fixtures", 1)[0]
    shell = os.environ.get("DYNLEX_TEST_BASH", "bash")
    with tempfile.TemporaryDirectory(prefix="dynlex-suite-groups-") as directory:
        root = Path(directory)
        rendezvous = root / "rendezvous.py"
        rendezvous.write_text('''import sys,time
from pathlib import Path
root=Path(sys.argv[1])
mode=sys.argv[2]
if mode == "fixture": (root/"started").touch()
deadline=time.monotonic()+5
waiting=root/("released" if mode == "fixture" else "started")
while not waiting.exists():
    if time.monotonic()>deadline: raise SystemExit("suite groups did not overlap")
    time.sleep(0.01)
if mode == "auxiliary": (root/"released").touch()
''')
        setup = '''
set -uo pipefail
TEST_OUTPUT_DIR=$1
PYTHON=$2
passed=0; failed=0; skipped=0; test_output=""; failures=()
'''
        check = '''
run_fixture_tests() {
    "$PYTHON" "$TEST_OUTPUT_DIR/rendezvous.py" "$TEST_OUTPUT_DIR" fixture || return
    passed=2; failed=1; skipped=1
    test_output=$'fixture output\\nsecond line\\n'
    failures=("fixture with whitespace")
}
start_fixture_tests
"$PYTHON" "$TEST_OUTPUT_DIR/rendezvous.py" "$TEST_OUTPUT_DIR" auxiliary || exit
passed=1; failed=1; skipped=0
test_output=$'auxiliary output\\n'; failures=(auxiliary)
collect_fixture_tests || exit
printf '%s/%s/%s\\n%s' "$passed" "$failed" "$skipped" "$test_output"
printf 'failure:%s\\n' "${failures[@]}"
'''
        result = subprocess.run([shell, "-c", setup + helpers + check, "suite-groups", root.as_posix(), sys.executable],
                                capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, result.stderr
        assert result.stdout == ("3/2/1\nfixture output\nsecond line\nauxiliary output\n"
                                 "failure:fixture with whitespace\nfailure:auxiliary\n"), result.stdout
        failure = '''
run_fixture_tests() { return 17; }
start_fixture_tests
collect_fixture_tests
'''
        result = subprocess.run([shell, "-c", setup + helpers + failure, "suite-groups", root.as_posix(), sys.executable],
                                capture_output=True, text=True, timeout=10)
        assert result.returncode == 17, "a crashed fixture worker's status was lost"


if __name__ == "__main__":
    main()
