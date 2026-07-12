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

append_test_result() {
    local status="$1"
    local color="$2"
    local test_name="$3"
    local detail="${4:-}"
    local elapsed_ms="${5:-}"
    local line="${color}${status}${NC} $test_name"

    if [[ -n "$detail" ]]; then
        line+=" ($detail"
        if [[ -n "$elapsed_ms" ]]; then
            line+=", ${elapsed_ms} ms"
        fi
        line+=")"
    elif [[ -n "$elapsed_ms" ]]; then
        line+=" (${elapsed_ms} ms)"
    fi

    test_output+="${line}\n"
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

build_output_has_diagnostics() {
    local output="$1"
    grep -Eqi '(^|[^[:alpha:]])(warning|error|fatal error)(:|[^[:alpha:]])' <<< "$output"
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
build_output=$("$SCRIPT_DIR/build.sh" 2>&1)
build_exit=$?
if [[ $build_exit -ne 0 ]]; then
    [[ -n "$build_output" ]] && printf "%s\n" "$build_output"
    echo -e "${RED}Build failed.${NC}"
    exit $build_exit
fi
if [[ -n "$build_output" ]] && build_output_has_diagnostics "$build_output"; then
    printf "%s\n" "$build_output"
fi

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
        append_test_result "FAIL" "$RED" "$test_name" "missing main.dl" "$test_elapsed_ms"
        ((failed++))
        failures+=("$test_name")
        continue
    fi

    expected_diagnostics_file="$test_dir/expected_diagnostics.txt"

    # Compile (5 second timeout)
    compile_output=$(run_with_timeout 5 "$COMPILER" "$source_file" -o "$output_binary" 2>&1)
    compile_exit=$?
    if [[ $compile_exit -eq 124 ]]; then
        test_elapsed_ms=$(elapsed_ms_since "$test_start_ms")
        append_test_result "FAIL" "$RED" "$test_name" "compilation timed out" "$test_elapsed_ms"
        ((failed++))
        failures+=("$test_name")
        continue
    fi
    if [[ $compile_exit -ge 128 ]]; then
        test_elapsed_ms=$(elapsed_ms_since "$test_start_ms")
        signal=$((compile_exit - 128))
        append_test_result "FAIL" "$RED" "$test_name" "compiler crashed with signal ${signal}" "$test_elapsed_ms"
        [[ -n "$compile_output" ]] && test_output+="  $compile_output\n"
        ((failed++))
        failures+=("$test_name")
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

    if [[ "$normalized_compile_output" != "$normalized_expected_diagnostics" ]]; then
        test_elapsed_ms=$(elapsed_ms_since "$test_start_ms")
        if [[ "$has_expected_diagnostics" == "true" ]]; then
            append_test_result "FAIL" "$RED" "$test_name" "diagnostics mismatch" "$test_elapsed_ms"
            test_output+="  Expected diagnostics: $(head -c 400 <<< "$expected_diagnostics")\n"
            test_output+="  Actual diagnostics:   $(head -c 400 <<< "$compile_output")\n"
        else
            append_test_result "FAIL" "$RED" "$test_name" "unexpected diagnostics" "$test_elapsed_ms"
            test_output+="  Actual diagnostics: $(head -c 400 <<< "$compile_output")\n"
        fi
        ((failed++))
        failures+=("$test_name")
        continue
    fi

    if [[ $compile_exit -ne 0 || "$output_binary_exists" != "true" ]]; then
        if [[ "$has_expected_diagnostics" == "true" && ! -f "$expected_file" ]]; then
            test_elapsed_ms=$(elapsed_ms_since "$test_start_ms")
            append_test_result "PASS" "$GREEN" "$test_name" "" "$test_elapsed_ms"
            ((passed++))
        else
            test_elapsed_ms=$(elapsed_ms_since "$test_start_ms")
            if [[ "$output_binary_exists" != "true" ]]; then
                append_test_result "FAIL" "$RED" "$test_name" "compilation did not produce runnable output" "$test_elapsed_ms"
            else
                append_test_result "FAIL" "$RED" "$test_name" "compilation failed" "$test_elapsed_ms"
            fi
            [[ -n "$compile_output" ]] && test_output+="  $compile_output\n"
            ((failed++)) || true
            failures+=("$test_name")
        fi
        continue
    fi

    # Compilation succeeded but this test only expected diagnostics from a failed compile
    if [[ "$has_expected_diagnostics" == "true" && ! -f "$expected_file" ]]; then
        test_elapsed_ms=$(elapsed_ms_since "$test_start_ms")
        append_test_result "FAIL" "$RED" "$test_name" "expected compile diagnostics without runnable output, but compilation succeeded" "$test_elapsed_ms"
        ((failed++))
        failures+=("$test_name")
        continue
    fi

    # Run (5 second timeout)
    actual_output=$(run_with_timeout 5 "$output_binary" 2>&1)
    run_exit=$?
    if [[ $run_exit -eq 124 ]]; then
        test_elapsed_ms=$(elapsed_ms_since "$test_start_ms")
        append_test_result "FAIL" "$RED" "$test_name" "execution timed out" "$test_elapsed_ms"
        ((failed++))
        failures+=("$test_name")
        continue
    fi
    if [[ $run_exit -ne 0 ]]; then
        test_elapsed_ms=$(elapsed_ms_since "$test_start_ms")
        append_test_result "FAIL" "$RED" "$test_name" "runtime error, exit $run_exit" "$test_elapsed_ms"
        test_output+="  $actual_output\n"
        ((failed++))
        failures+=("$test_name")
        continue
    fi

    # Compare
    expected_output=""
    if [[ -f "$expected_file" ]]; then
        expected_output=$(normalize_output "$(cat "$expected_file")")
    fi
    normalized_actual_output=$(normalize_output "$actual_output")
    if [[ "$normalized_actual_output" == "$expected_output" ]]; then
        test_elapsed_ms=$(elapsed_ms_since "$test_start_ms")
        append_test_result "PASS" "$GREEN" "$test_name" "" "$test_elapsed_ms"
        ((passed++))
    else
        test_elapsed_ms=$(elapsed_ms_since "$test_start_ms")
        append_test_result "FAIL" "$RED" "$test_name" "output mismatch" "$test_elapsed_ms"
        test_output+="  Expected: $(head -c 200 <<< "$expected_output")\n"
        test_output+="  Actual:   $(head -c 200 <<< "$actual_output")\n"
        ((failed++))
        failures+=("$test_name")
    fi
done

lsp_test_start_ms=$(now_ms)
lsp_test_output=$(run_with_timeout 30 "$SCRIPT_DIR/test_lsp.sh" 2>&1)
lsp_test_exit=$?
lsp_test_elapsed_ms=$(elapsed_ms_since "$lsp_test_start_ms")
if [[ $lsp_test_exit -eq 0 ]]; then
    append_test_result "PASS" "$GREEN" "lsp_integration" "" "$lsp_test_elapsed_ms"
    ((passed++))
else
    append_test_result "FAIL" "$RED" "lsp_integration" "exit $lsp_test_exit" "$lsp_test_elapsed_ms"
    [[ -n "$lsp_test_output" ]] && test_output+="  $lsp_test_output\n"
    ((failed++)) || true
    failures+=("lsp_integration")
fi

total_elapsed_ms=$(elapsed_ms_since "$total_start_ms")

# Only show per-test details if there are failures
if [[ $failed -gt 0 ]]; then
    echo -e "$test_output"
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
