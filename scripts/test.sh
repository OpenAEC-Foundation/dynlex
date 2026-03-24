#!/bin/bash
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
COMPILER="$PROJECT_DIR/build/dynlex"
if [[ ! -x "$COMPILER" && -x "$PROJECT_DIR/build/dynlex.exe" ]]; then
    COMPILER="$PROJECT_DIR/build/dynlex.exe"
fi
TESTS_DIR="$PROJECT_DIR/tests/required"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m'

passed=0
failed=0
skipped=0
failures=()
failure_timings=()
test_output=""
is_windows=false
case "$(uname -s | tr '[:upper:]' '[:lower:]')" in
    *mingw*|*msys*|*cygwin*) is_windows=true ;;
esac

# Known failing tests — these don't count as unexpected failures
KNOWN_FAILURES=""

now_ms() {
    local timestamp
    timestamp=$(date +%s%3N 2>/dev/null || true)
    if [[ "$timestamp" =~ ^[0-9]+$ ]]; then
        printf "%s\n" "$timestamp"
        return
    fi
    timestamp=$(date +%s 2>/dev/null || true)
    if [[ "$timestamp" =~ ^[0-9]+$ ]]; then
        printf "%s\n" "$((timestamp * 1000))"
        return
    fi
    printf "0\n"
}

elapsed_ms_since() {
    local start_ms="$1"
    local end_ms
    end_ms=$(now_ms)
    printf "%s\n" "$((end_ms - start_ms))"
}

normalize_output() {
    printf "%s" "$1" | tr -d '\r'
}

normalize_diagnostics() {
    PYTHON_DIAGNOSTICS="$1" python3 - "$PROJECT_DIR" <<'PY'
import re
import sys
import os

project_dir = sys.argv[1].replace("\\", "/").rstrip("/")
text = os.environ["PYTHON_DIAGNOSTICS"].replace("\r", "")

path_prefixes = {project_dir}
match = re.match(r"^/([A-Za-z])/(.*)$", project_dir)
if match:
    drive = match.group(1)
    rest = match.group(2)
    path_prefixes.add(f"{drive.upper()}:/{rest}")
    path_prefixes.add(f"{drive.lower()}:/{rest}")

for prefix in sorted(path_prefixes, key=len, reverse=True):
    text = text.replace(prefix + "/", "")

sys.stdout.write("\n".join(line.rstrip() for line in text.splitlines()).rstrip())
PY
}

run_with_timeout() {
    local seconds="$1"
    shift
    if command -v timeout >/dev/null 2>&1; then
        timeout "$seconds" "$@"
        return $?
    fi
    if command -v gtimeout >/dev/null 2>&1; then
        gtimeout "$seconds" "$@"
        return $?
    fi
    python3 - "$seconds" "$@" <<'PY'
import subprocess
import sys

timeout_seconds = int(sys.argv[1])
cmd = sys.argv[2:]

try:
    completed = subprocess.run(cmd, timeout=timeout_seconds, capture_output=True)
except subprocess.TimeoutExpired as exc:
    stderr = exc.stderr or b""
    if stderr:
        sys.stderr.buffer.write(stderr)
    sys.exit(124)

if completed.stdout:
    sys.stdout.buffer.write(completed.stdout)
if completed.stderr:
    sys.stderr.buffer.write(completed.stderr)
sys.exit(completed.returncode)
PY
}

echo -e "${YELLOW}Building compiler...${NC}"
"$SCRIPT_DIR/build.sh"

total_start_ms=$(now_ms)

for test_dir in "$TESTS_DIR"/*/; do
    test_start_ms=$(now_ms)
    test_dir="${test_dir%/}"
    test_name="$(basename "$test_dir")"
    source_file="$test_dir/main.dl"
    expected_file="$test_dir/expected.txt"
    output_binary="$test_dir/main.out"
    if [[ "$is_windows" == "true" ]]; then
        output_binary="$test_dir/main.exe"
    fi

    if [[ ! -f "$source_file" ]]; then
        test_elapsed_ms=$(elapsed_ms_since "$test_start_ms")
        test_output+="${RED}FAIL${NC} $test_name (missing main.dl, ${test_elapsed_ms} ms)\n"
        ((failed++))
        failures+=("$test_name")
        failure_timings+=("$test_name:${test_elapsed_ms}")
        continue
    fi

    expected_diagnostics_file="$test_dir/expected_diagnostics.txt"
    legacy_expected_error_file="$test_dir/expected_error.txt"
    if [[ ! -f "$expected_diagnostics_file" && -f "$legacy_expected_error_file" ]]; then
        expected_diagnostics_file="$legacy_expected_error_file"
    fi

    if [[ ! -f "$expected_file" && ! -f "$expected_diagnostics_file" ]]; then
        test_output+="${YELLOW}SKIP${NC} $test_name (no expected.txt or expected_diagnostics.txt)\n"
        ((skipped++))
        continue
    fi

    # Compile (5 second timeout)
    compile_output=$(run_with_timeout 5 "$COMPILER" "$source_file" -o "$output_binary" 2>&1)
    compile_exit=$?
    if [[ $compile_exit -eq 124 ]]; then
        test_elapsed_ms=$(elapsed_ms_since "$test_start_ms")
        test_output+="${RED}FAIL${NC} $test_name (compilation timed out, ${test_elapsed_ms} ms)\n"
        ((failed++))
        failures+=("$test_name")
        failure_timings+=("$test_name:${test_elapsed_ms}")
        continue
    fi
    output_binary_exists=false
    if [[ -f "$output_binary" ]]; then
        output_binary_exists=true
    fi
    if [[ "$is_windows" != "true" && ! -x "$output_binary" ]]; then
        output_binary_exists=false
    fi

    normalized_compile_output=$(normalize_diagnostics "$compile_output")
    expected_diagnostics=""
    normalized_expected_diagnostics=""
    has_expected_diagnostics=false
    if [[ -f "$expected_diagnostics_file" ]]; then
        expected_diagnostics=$(<"$expected_diagnostics_file")
        normalized_expected_diagnostics=$(normalize_diagnostics "$expected_diagnostics")
        has_expected_diagnostics=true
    fi

    if [[ "$has_expected_diagnostics" == "true" ]]; then
        if [[ "$normalized_compile_output" != "$normalized_expected_diagnostics" ]]; then
            test_elapsed_ms=$(elapsed_ms_since "$test_start_ms")
            test_output+="${RED}FAIL${NC} $test_name (diagnostics mismatch)\n"
            test_output+="  Expected diagnostics: $(head -c 400 <<< "$expected_diagnostics")\n"
            test_output+="  Actual diagnostics:   $(head -c 400 <<< "$compile_output")\n"
            test_output+="  Elapsed: ${test_elapsed_ms} ms\n"
            ((failed++))
            failures+=("$test_name")
            failure_timings+=("$test_name:${test_elapsed_ms}")
            continue
        fi
    elif [[ -n "$normalized_compile_output" ]]; then
        test_elapsed_ms=$(elapsed_ms_since "$test_start_ms")
        test_output+="${RED}FAIL${NC} $test_name (unexpected diagnostics)\n"
        test_output+="  Actual diagnostics: $(head -c 400 <<< "$compile_output")\n"
        test_output+="  Elapsed: ${test_elapsed_ms} ms\n"
        ((failed++))
        failures+=("$test_name")
        failure_timings+=("$test_name:${test_elapsed_ms}")
        continue
    fi

    if [[ $compile_exit -ne 0 || "$output_binary_exists" != "true" ]]; then
        if [[ "$has_expected_diagnostics" == "true" && ! -f "$expected_file" ]]; then
            test_output+="${GREEN}PASS${NC} $test_name\n"
            ((passed++))
        else
            test_elapsed_ms=$(elapsed_ms_since "$test_start_ms")
            if [[ "$output_binary_exists" != "true" ]]; then
                test_output+="${RED}FAIL${NC} $test_name (compilation did not produce runnable output, ${test_elapsed_ms} ms)\n"
            else
                test_output+="${RED}FAIL${NC} $test_name (compilation failed, ${test_elapsed_ms} ms)\n"
            fi
            [[ -n "$compile_output" ]] && test_output+="  $compile_output\n"
            ((failed++)) || true
            failures+=("$test_name")
            failure_timings+=("$test_name:${test_elapsed_ms}")
        fi
        continue
    fi

    # Compilation succeeded but this test only expected diagnostics from a failed compile
    if [[ "$has_expected_diagnostics" == "true" && ! -f "$expected_file" ]]; then
        test_elapsed_ms=$(elapsed_ms_since "$test_start_ms")
        test_output+="${RED}FAIL${NC} $test_name (expected compile diagnostics without runnable output, but compilation succeeded, ${test_elapsed_ms} ms)\n"
        ((failed++))
        failures+=("$test_name")
        failure_timings+=("$test_name:${test_elapsed_ms}")
        continue
    fi

    # Run (5 second timeout)
    actual_output=$(run_with_timeout 5 "$output_binary" 2>&1)
    run_exit=$?
    if [[ $run_exit -eq 124 ]]; then
        test_elapsed_ms=$(elapsed_ms_since "$test_start_ms")
        test_output+="${RED}FAIL${NC} $test_name (execution timed out, ${test_elapsed_ms} ms)\n"
        ((failed++))
        failures+=("$test_name")
        failure_timings+=("$test_name:${test_elapsed_ms}")
        continue
    fi
    if [[ $run_exit -ne 0 ]]; then
        test_elapsed_ms=$(elapsed_ms_since "$test_start_ms")
        test_output+="${RED}FAIL${NC} $test_name (runtime error, exit $run_exit, ${test_elapsed_ms} ms)\n"
        test_output+="  $actual_output\n"
        ((failed++))
        failures+=("$test_name")
        failure_timings+=("$test_name:${test_elapsed_ms}")
        continue
    fi

    # Compare
    expected_output=$(normalize_output "$(cat "$expected_file")")
    normalized_actual_output=$(normalize_output "$actual_output")
    if [[ "$normalized_actual_output" == "$expected_output" ]]; then
        test_output+="${GREEN}PASS${NC} $test_name\n"
        ((passed++))
    else
        test_elapsed_ms=$(elapsed_ms_since "$test_start_ms")
        test_output+="${RED}FAIL${NC} $test_name (output mismatch)\n"
        test_output+="  Expected: $(head -c 200 <<< "$expected_output")\n"
        test_output+="  Actual:   $(head -c 200 <<< "$actual_output")\n"
        test_output+="  Elapsed: ${test_elapsed_ms} ms\n"
        ((failed++))
        failures+=("$test_name")
        failure_timings+=("$test_name:${test_elapsed_ms}")
    fi
done

total_elapsed_ms=$(elapsed_ms_since "$total_start_ms")

# Only show per-test details if there are failures
if [[ $failed -gt 0 ]]; then
    echo -e "$test_output"
    echo "Failure timings (ms):"
    for entry in "${failure_timings[@]}"; do
        echo "  ${entry/:/: } ms"
    done
fi

echo "Results: ${passed} passed, ${failed} failed, ${skipped} skipped (${total_elapsed_ms} ms total)"

if [[ ${#failures[@]} -gt 0 ]]; then
    # Separate known vs unexpected failures
    unexpected=()
    known=()
    for f in "${failures[@]}"; do
        if [[ " $KNOWN_FAILURES " == *" $f "* ]]; then
            known+=("$f")
        else
            unexpected+=("$f")
        fi
    done

    if [[ ${#known[@]} -gt 0 ]]; then
        echo -e "${YELLOW}Known failing tests: ${known[*]}${NC}"
    fi
    if [[ ${#unexpected[@]} -gt 0 ]]; then
        echo -e "${RED}Unexpected failures: ${unexpected[*]}${NC}"
        exit 1
    fi
fi
