#!/usr/bin/env python3
"""Independent suite groups overlap and retain every result and worker failure."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    source = Path(__file__).with_name("test.sh").read_text()
    helpers = source.split("start_test_group() {\n", 1)
    assert len(helpers) == 2, "independent required groups need reusable worker handling"
    helpers = "start_test_group() {\n" + helpers[1].split("\n# Run fixtures", 1)[0]
    shell = os.environ.get("DYNLEX_TEST_BASH", "bash")
    with tempfile.TemporaryDirectory(prefix="dynlex-suite-groups-") as directory:
        root = Path(directory)
        rendezvous = root / "rendezvous.py"
        rendezvous.write_text('''import sys,time
from pathlib import Path
root=Path(sys.argv[1])
mode=sys.argv[2]
if mode != "auxiliary": (root/(mode+"-started")).touch()
deadline=time.monotonic()+5
waiting=[root/"released"] if mode != "auxiliary" else [root/"fixture-started",root/"incremental-started"]
while not all(path.exists() for path in waiting):
    if time.monotonic()>deadline: raise SystemExit("suite groups did not overlap")
    time.sleep(0.01)
if mode == "auxiliary": (root/"released").touch()
''')
        setup = '''
set -uo pipefail
TEST_OUTPUT_DIR=$1
PYTHON=$2
passed=11; failed=7; skipped=9; test_output="parent"; failures=(parent)
'''
        check = '''
run_fixture_tests() {
    "$PYTHON" "$TEST_OUTPUT_DIR/rendezvous.py" "$TEST_OUTPUT_DIR" fixture || return
    passed=$((passed+2)); failed=$((failed+1)); skipped=$((skipped+1))
    test_output=$'fixture output\\nsecond line\\n'
    failures=("fixture with whitespace")
}
run_incremental_tests() {
    "$PYTHON" "$TEST_OUTPUT_DIR/rendezvous.py" "$TEST_OUTPUT_DIR" incremental || return
    passed=$((passed+3)); skipped=$((skipped+2))
    test_output=$'incremental output\n'
}
start_test_group fixture run_fixture_tests
fixture_worker=$test_group_worker
start_test_group incremental run_incremental_tests
incremental_worker=$test_group_worker
"$PYTHON" "$TEST_OUTPUT_DIR/rendezvous.py" "$TEST_OUTPUT_DIR" auxiliary || exit
passed=1; failed=1; skipped=0
test_output=$'auxiliary output\\n'; failures=(auxiliary)
collect_test_group "$incremental_worker" incremental || exit
collect_test_group "$fixture_worker" fixture || exit
printf '%s/%s/%s\\n%s' "$passed" "$failed" "$skipped" "$test_output"
printf 'failure:%s\\n' "${failures[@]}"
'''
        result = subprocess.run([shell, "-c", setup + helpers + check, "suite-groups", root.as_posix(), sys.executable],
                                capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, result.stderr
        assert result.stdout == ("6/2/3\nfixture output\nsecond line\nincremental output\nauxiliary output\n"
                                 "failure:fixture with whitespace\nfailure:auxiliary\n"), result.stdout
        failure = '''
crash() { return 17; }
start_test_group crashing crash
collect_test_group "$test_group_worker" crashing
'''
        result = subprocess.run([shell, "-c", setup + helpers + failure, "suite-groups", root.as_posix(), sys.executable],
                                capture_output=True, text=True, timeout=10)
        assert result.returncode == 17, "a crashed suite worker's status was lost"


if __name__ == "__main__":
    main()
