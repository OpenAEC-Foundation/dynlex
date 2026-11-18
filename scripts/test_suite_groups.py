#!/usr/bin/env python3
"""Independent suite groups overlap and retain every result and worker failure."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile


def check_fixture_partitions(source, shell):
    start = source.index('run_fixture_tests() {\n')
    end = source.index('\nreturn 0\n}', start) + len('\nreturn 0\n}')
    fixture_runner = source[start:end]
    with tempfile.TemporaryDirectory(prefix='dynlex-fixture-partitions-') as temporary:
        root = Path(temporary)
        for name in ('alpha', 'beta', 'delta', 'epsilon', 'gamma'):
            fixture = root / name
            fixture.mkdir()
            (fixture / 'main.dl').write_text('@intrinsic("discard", 0)\n')
        compiler = root / 'compiler.sh'
        compiler.write_text('''#!/bin/bash
source=$1; shift
while [[ $# -gt 0 ]]; do
    if [[ "$1" == -o ]]; then output=$2; shift; fi
    shift
done
printf '%s|%s\\n' "$source" "$output" >> "$COMPILE_LOG"
printf '#!/bin/sh\\nexit 0\\n' > "$output"
chmod +x "$output"
''')
        compiler.chmod(0o755)
        setup = '''
set -uo pipefail
TESTS_DIR=$1; TEST_OUTPUT_DIR=$1/output; COMPILER=$1/compiler.sh
mkdir "$TEST_OUTPUT_DIR"
export COMPILE_LOG=$1/compiled
source "$2/test_fixture_options.sh"
suite=required; is_windows=false; execution_timeout_scale=1
RED=""; GREEN=""; YELLOW=""; KNOWN_FAILURES=""
passed=0; failed=0; skipped=0; failures=(); test_output=""
now_ms() { printf '0\\n'; }
elapsed_ms_since() { printf '0\\n'; }
append_test_result() { :; }
append_test_output() { :; }
normalize_output() { printf '%s' "$1"; }
run_with_timeout() { shift; "$@"; }
'''
        # Keep the output directory outside the fixture glob.
        setup = setup.replace('TESTS_DIR=$1;', 'TESTS_DIR=$1/fixtures;')
        fixtures = root / 'fixtures'
        fixtures.mkdir()
        for fixture in list(root.glob('*/main.dl')):
            fixture.parent.rename(fixtures / fixture.parent.name)
        checks = '''
(run_fixture_tests 0 2; [[ $failed == 0 ]]) & first=$!
(run_fixture_tests 1 2; [[ $failed == 0 ]]) & second=$!
wait "$first" && wait "$second"
'''
        result = subprocess.run([shell, '-c', setup + fixture_runner + checks,
                                 'fixture-partitions', root.as_posix(), Path(__file__).parent.as_posix()],
                                capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, (result.stdout, result.stderr)
        rows = [line.split('|') for line in (root / 'compiled').read_text().splitlines()]
        assert sorted(Path(row[0]).parent.name for row in rows) == \
            ['alpha', 'beta', 'delta', 'epsilon', 'gamma'], 'fixtures were skipped or compiled twice'
        assert len({Path(row[1]).parent for row in rows}) == 2, 'fixture workers share an executable path'


def main():
    source = Path(__file__).with_name("test.sh").read_text()
    helpers = Path(__file__).with_name("test_suite_groups.sh").read_text()
    shell = os.environ.get("DYNLEX_TEST_BASH", "bash")
    check_fixture_partitions(source, shell)
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
collect_test_group incremental "$incremental_worker" || exit
collect_test_group fixture "$fixture_worker" || exit
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
collect_test_group crashing "$test_group_worker"
'''
        result = subprocess.run([shell, "-c", setup + helpers + failure, "suite-groups", root.as_posix(), sys.executable],
                                capture_output=True, text=True, timeout=10)
        assert result.returncode == 17, "a crashed suite worker's status was lost"

        # Two named workers must rendezvous before the parent joins either one.
        # Each contributes its own counters, output and whitespace-bearing errors.
        (root / "workers.py").write_text('''import sys,time
from pathlib import Path
root=Path(sys.argv[1]); name=sys.argv[2]
(root/('worker-'+name)).touch()
deadline=time.monotonic()+5
while not all((root/('worker-'+other)).exists() for other in ('one','two')):
    if time.monotonic()>deadline: raise SystemExit('independent workers did not overlap')
    time.sleep(0.01)
''')
        concurrent = '''
checks() {
    "$PYTHON" "$TEST_OUTPUT_DIR/workers.py" "$TEST_OUTPUT_DIR" "$1" || return
    passed=$2; failed=1; skipped=1
    test_output="$1 output"$'\n'
    failures=("$1 failure with whitespace")
}
passed=9; failed=9; skipped=9
start_test_group one checks one 2
one_worker=$test_group_worker
start_test_group two checks two 3
two_worker=$test_group_worker
passed=1; failed=0; skipped=0; test_output=""; failures=()
collect_test_group one "$one_worker" || exit
collect_test_group two "$two_worker" || exit
printf '%s/%s/%s\n%s' "$passed" "$failed" "$skipped" "$test_output"
printf 'failure:%s\n' "${failures[@]}"
'''
        result = subprocess.run([shell, "-c", setup + helpers + concurrent,
                                 "independent-suite-groups", root.as_posix(), sys.executable],
                                capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, result.stderr
        assert result.stdout == ("6/2/2\ntwo output\none output\n"
                                 "failure:two failure with whitespace\n"
                                 "failure:one failure with whitespace\n"), result.stdout

        auxiliary = ("run_auxiliary_test() {\n" + source.split("run_auxiliary_test() {\n", 1)[1]
                     .split("\n}\n", 1)[0] + "\n}\n")
        statuses = '''
suite=required; execution_timeout_scale=1; GREEN=""; RED=""
now_ms() { printf '0\n'; }
elapsed_ms_since() { printf '0\n'; }
run_with_timeout() { shift; "$@"; }
append_test_result() { :; }
append_test_output() { :; }
passed=0; failed=0; skipped=0; test_output=""; failures=()
start_test_group passing run_auxiliary_test incremental_compilation 10 true
passing_worker=$test_group_worker
collect_test_group passing "$passing_worker" || exit
start_test_group failing run_auxiliary_test incremental_compilation 10 false
failing_worker=$test_group_worker
collect_test_group failing "$failing_worker" || exit
printf '%s/%s/%s\n' "$passed" "$failed" "$skipped"
printf 'failure:%s\n' "${failures[@]}"
'''
        result = subprocess.run([shell, "-c", setup + helpers + auxiliary + statuses,
                                 "auxiliary-statuses", root.as_posix(), sys.executable],
                                capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, result.stderr
        assert result.stdout == ("Testing incremental_compilation...\n" * 2
                                 + "1/1/0\nfailure:incremental_compilation\n"), result.stdout

        joins = '''
crashed() { return 17; }
completed() { passed=2; test_output="completed output"; }
dependencies() { passed=3; test_output="dependency output"; }
passed=0; failed=0; skipped=0; test_output=""; failures=()
start_test_group fixture crashed
fixture_worker=$test_group_worker
start_test_group fixture_parallel completed
fixture_parallel_worker=$test_group_worker
start_test_group incremental completed
incremental_worker=$test_group_worker
start_test_group incremental_dependencies dependencies
incremental_dependencies_worker=$test_group_worker
start_test_group incremental_native dependencies
incremental_native_worker=$test_group_worker
status=0
collect_required_tests || status=$?
printf '%s/%s/%s\n' "$status" "$passed" "$test_output"
'''
        result = subprocess.run([shell, "-c", setup + helpers + joins,
                                 "joined-suite-workers", root.as_posix(), sys.executable],
                                capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, result.stderr
        assert result.stdout == "17/10/dependency outputdependency outputcompleted outputcompleted output\n", result.stdout


if __name__ == "__main__":
    main()
