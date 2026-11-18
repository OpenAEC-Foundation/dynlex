#!/usr/bin/env python3
"""Keep the short compiler gate independent of the application libraries."""

from pathlib import Path
import os
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1] / "tests" / "required"


def check_runner_policy():
    runner = ROOT.parents[1] / "scripts" / "test.sh"
    source = runner.read_text(encoding="utf-8")
    routing = "run_auxiliary_test() {\n" + source.split("run_auxiliary_test() {\n", 1)[1].split("\n}\n", 1)[0] + "\n}\n"
    shell = os.environ.get("DYNLEX_TEST_BASH", "bash")
    stubs = '''
now_ms() { printf -v "$1" 0; }
elapsed_ms_since() { printf -v "$2" 0; }
run_with_timeout() { return 0; }
append_test_result() { :; }
execution_timeout_scale=1
passed=0
failed=0
'''
    failures = []
    cases = {
        "numeric_literals": ("required",),
        "class_layout": ("required",),
        "incremental_compilation": ("required",),
        "incremental_cache_entries": ("required",),
        "pattern_precedence": ("required",),
        "associative_grouping": ("required",),
        "compiler_stack": ("required",),
        "timeout_output": ("required",),
        "incremental_regression_base": ("integration", "compiler-regressions"),
        "incremental_regression_dependencies": ("integration", "compiler-regressions"),
        "incremental_regression_native": ("integration", "compiler-regressions"),
        "incremental_compilation_integration": ("integration",),
    }
    for suite in ("required", "integration", "compiler-regressions"):
        result = subprocess.run(
            [shell, "-c", stubs + routing + '\nsuite=$1; shift\nfor name; do run_auxiliary_test "$name" 10 true; done\n',
             "suite-policy", suite, *cases],
            capture_output=True, text=True, timeout=10,
        )
        expected = [f"Testing {name}..." for name, expected_suites in cases.items() if suite in expected_suites]
        if result.returncode != 0 or result.stdout.splitlines() != expected:
            failures.append(f"{runner}: incorrect {suite} routing: {result.stdout}{result.stderr}")

    preparation = 'if [[ "$run_only" != "true" ]]; then\n'
    build_phase = preparation + source.split(preparation, 1)[1].split('\nif ! (cd "$PROJECT_DIR"', 1)[0]
    with tempfile.TemporaryDirectory(prefix="dynlex-required-boundary-") as directory:
        build = Path(directory) / "build.sh"
        build.write_text("#!/bin/sh\nprintf 'COMPILER_BUILD:%s\\n' \"$@\"\n", encoding="utf-8", newline="\n")
        build.chmod(0o755)
        for run_only, suite_worker, expected in (
            (False, False, ["COMPILER_BUILD:--test-tools", "TIMED_SUITE:120"]),
            (True, False, ["TIMED_SUITE:120"]),
            (True, True, []),
        ):
            result = subprocess.run(
                [shell, "-c", '''
run_only=$3
suite_worker=$4
suite=required
SCRIPT_DIR=$1
PROJECT_DIR=$1
DYNLEX_TEST_BASH=$2
build_args=()
cmake() { printf 'UNEXPECTED_PREBUILD\\n'; return 1; }
run_with_timeout() { printf 'TIMED_SUITE:%s\\n' "$1"; }
''' + build_phase, "required-boundary", Path(directory).as_posix(), shell,
                 str(run_only).lower(), str(suite_worker).lower()],
                capture_output=True, text=True, timeout=10,
            )
            events = [line for line in result.stdout.splitlines()
                      if line.startswith(("COMPILER_BUILD", "UNEXPECTED_PREBUILD", "TIMED_SUITE:"))]
            if result.returncode != 0 or events != expected:
                failures.append(f"{runner}: incorrect required budget boundary (run-only: {run_only}, worker: {suite_worker}): {result.stdout}{result.stderr}")

        compile_phase = source.split('    rm -f "$output_binary"\n', 1)[1].split('    compile_exit=$?', 1)[0]
        stack_limit = Path(directory) / "stack_limit_kb.txt"
        for suite in ("required", "integration"):
            for constrained_stack in (False, True):
                if constrained_stack:
                    stack_limit.write_text("1024\n")
                elif stack_limit.exists():
                    stack_limit.unlink()
                result = subprocess.run(
                    [shell, "-c", '''
COMPILER=compiler
source_file=fixture.dl
output_binary=$1/program.out
stack_limit_file=$1/stack_limit_kb.txt
suite=$2
is_windows=false
compile_timeout=10
run_with_timeout() { printf '%s\\n' "$@"; }
''' + compile_phase + '\nprintf "%s\\n" "$compile_output"\n',
                     "fixture-cache-policy", Path(directory).as_posix(), suite],
                    capture_output=True, text=True, timeout=10,
                )
                clean = "--no-incremental" in result.stdout.splitlines()
                if result.returncode != 0 or clean != (suite == "required"):
                    failures.append(f"{runner}: {suite} fixture cache policy is incorrect (stack limit: {constrained_stack}): {result.stdout}{result.stderr}")
    return failures


def main() -> int:
    failures = check_runner_policy()
    fixtures = sorted(path for path in ROOT.iterdir() if path.is_dir())
    for fixture in fixtures:
        if not (fixture / "main.dl").is_file():
            failures.append(f"{fixture}: missing main.dl")
        if (fixture / "compile_timeout_seconds.txt").exists():
            failures.append(f"{fixture}: focused tests use the default compile budget")
        for source in fixture.rglob("*.dl"):
            for number, line in enumerate(source.read_text().splitlines(), 1):
                stripped = line.strip()
                if not stripped.startswith("import "):
                    continue
                target = stripped[7:].split("#", 1)[0].strip()
                local_target = (source.parent / target).is_file() or (fixture / target).is_file()
                if "/" in target or "\\" in target or not local_target:
                    failures.append(f"{source}:{number}: required imports must use a local helper file")
    if not fixtures:
        failures.append(f"{ROOT}: no required fixtures")
    for failure in failures:
        print(failure, file=sys.stderr)
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
