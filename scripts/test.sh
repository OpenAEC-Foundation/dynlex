#!/bin/bash
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
TESTS_DIR="$PROJECT_DIR/tests/required"

is_windows=false
case "$(uname -s | tr '[:upper:]' '[:lower:]')" in
    *mingw*|*msys*|*cygwin*) is_windows=true ;;
esac

COMPILER="$PROJECT_DIR/build/dynlex"
if [[ "$is_windows" == "true" ]]; then
    COMPILER+=".exe"
fi

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
    local reject_line_numbers="${2:-false}"
    local arguments=("$PROJECT_DIR")
    if [[ "$reject_line_numbers" == "true" ]]; then
        arguments+=("--reject-line-numbers")
    fi
    printf "%s" "$1" | python3 -B "$SCRIPT_DIR/diagnostic_expectations.py" "${arguments[@]}"
}

run_with_timeout() {
    local seconds="$1"
    shift
    python3 - "$seconds" "$@" <<'PY'
import os
import signal
import subprocess
import sys

timeout_seconds = int(sys.argv[1])
cmd = sys.argv[2:]
popen_options = {
    "stdout": subprocess.PIPE,
    "stderr": subprocess.PIPE,
}
if os.name == "nt":
    popen_options["creationflags"] = subprocess.CREATE_NEW_PROCESS_GROUP
else:
    popen_options["start_new_session"] = True

process = subprocess.Popen(cmd, **popen_options)

try:
    stdout, stderr = process.communicate(timeout=timeout_seconds)
except subprocess.TimeoutExpired:
    termination_error = b""
    if os.name == "nt":
        terminated = subprocess.run(
            ["taskkill", "/PID", str(process.pid), "/T", "/F"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            check=False,
        )
        if terminated.returncode != 0:
            termination_error = terminated.stderr
            if process.poll() is None:
                process.kill()
    else:
        os.killpg(process.pid, signal.SIGKILL)

    try:
        stdout, stderr = process.communicate(timeout=5)
    except subprocess.TimeoutExpired as cleanup_timeout:
        if process.poll() is None:
            process.kill()
        if process.stdout is not None:
            process.stdout.close()
        if process.stderr is not None:
            process.stderr.close()
        process.wait(timeout=5)
        stdout = cleanup_timeout.output or b""
        stderr = cleanup_timeout.stderr or b""
        termination_error += b"Timed-out process tree did not release its output pipes\n"

    if stdout:
        sys.stdout.buffer.write(stdout)
    if stderr:
        sys.stderr.buffer.write(stderr)
    if termination_error:
        sys.stderr.buffer.write(termination_error)
        sys.exit(125)
    sys.exit(124)

if stdout:
    sys.stdout.buffer.write(stdout)
if stderr:
    sys.stderr.buffer.write(stderr)
if process.returncode < 0:
    signal_number = -process.returncode
    if signal_number <= 127:
        sys.exit(128 + signal_number)
    sys.stderr.write(f"Process terminated by signal {signal_number}\n")
    sys.exit(125)
if process.returncode > 255:
    status = process.returncode & 0xFFFFFFFF
    sys.stderr.write(f"Process terminated with Windows status 0x{status:08X}\n")
    sys.exit(125)
sys.exit(process.returncode)
PY
}

echo -e "${YELLOW}Building compiler...${NC}"
"$SCRIPT_DIR/build.sh" "$@"
build_exit=$?
if [[ $build_exit -ne 0 ]]; then
    echo -e "${RED}Build failed.${NC}"
    exit $build_exit
fi
echo -e "${GREEN}Compiler build complete.${NC}"

total_start_ms=$(now_ms)

for test_dir in "$TESTS_DIR"/*/; do
    test_start_ms=$(now_ms)
    test_dir="${test_dir%/}"
    test_name="$(basename "$test_dir")"
    echo "Testing $test_name..."
    source_file="$test_dir/main.dl"
    expected_file="$test_dir/expected.txt"
    output_binary="$test_dir/main.out"
    stack_limit_file="$test_dir/stack_limit_kb.txt"
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
    expected_diagnostics=""
    normalized_expected_diagnostics=""
    has_expected_diagnostics=false
    if [[ -f "$expected_diagnostics_file" ]]; then
        expected_diagnostics=$(<"$expected_diagnostics_file")
        normalized_expected_diagnostics=$(normalize_diagnostics "$expected_diagnostics" true 2>&1)
        expected_diagnostics_exit=$?
        if [[ $expected_diagnostics_exit -ne 0 ]]; then
            test_elapsed_ms=$(elapsed_ms_since "$test_start_ms")
            append_test_result "FAIL" "$RED" "$test_name" "invalid expected diagnostics" "$test_elapsed_ms"
            test_output+="  $normalized_expected_diagnostics\n"
            ((failed++))
            failures+=("$test_name")
            continue
        fi
        has_expected_diagnostics=true
    fi

    # Compile (5 second timeout)
    if [[ -f "$stack_limit_file" && "$is_windows" != "true" ]]; then
        stack_limit_kb=$(<"$stack_limit_file")
        if [[ ! "$stack_limit_kb" =~ ^[1-9][0-9]*$ ]]; then
            test_elapsed_ms=$(elapsed_ms_since "$test_start_ms")
            append_test_result "FAIL" "$RED" "$test_name" "invalid stack_limit_kb.txt" "$test_elapsed_ms"
            ((failed++))
            failures+=("$test_name")
            continue
        fi
        compile_output=$( (ulimit -s "$stack_limit_kb"; run_with_timeout 5 "$COMPILER" "$source_file" -o "$output_binary") 2>&1)
    else
        compile_output=$(run_with_timeout 5 "$COMPILER" "$source_file" -o "$output_binary" 2>&1)
    fi
    compile_exit=$?
    if [[ $compile_exit -eq 124 ]]; then
        test_elapsed_ms=$(elapsed_ms_since "$test_start_ms")
        append_test_result "FAIL" "$RED" "$test_name" "compilation timed out" "$test_elapsed_ms"
        ((failed++))
        failures+=("$test_name")
        continue
    fi
    if [[ $compile_exit -eq 125 ]]; then
        test_elapsed_ms=$(elapsed_ms_since "$test_start_ms")
        append_test_result "FAIL" "$RED" "$test_name" "compiler terminated abnormally" "$test_elapsed_ms"
        [[ -n "$compile_output" ]] && test_output+="  $compile_output\n"
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

    if [[ "$normalized_compile_output" != "$normalized_expected_diagnostics" ]]; then
        test_elapsed_ms=$(elapsed_ms_since "$test_start_ms")
        if [[ "$has_expected_diagnostics" == "true" ]]; then
            append_test_result "FAIL" "$RED" "$test_name" "diagnostics mismatch" "$test_elapsed_ms"
            test_output+="  Compiler exit:        $compile_exit\n"
            test_output+="  Expected diagnostics: $(head -c 400 <<< "$expected_diagnostics")\n"
            test_output+="  Actual diagnostics:   $(head -c 400 <<< "$compile_output")\n"
        else
            append_test_result "FAIL" "$RED" "$test_name" "unexpected diagnostics" "$test_elapsed_ms"
            test_output+="  Compiler exit:      $compile_exit\n"
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
                append_test_result "FAIL" "$RED" "$test_name" "compiler exited $compile_exit without runnable output" "$test_elapsed_ms"
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

run_auxiliary_test() {
    local test_name="$1"
    local timeout_seconds="$2"
    shift 2

    local test_start_ms
    local auxiliary_output
    local auxiliary_exit
    local test_elapsed_ms
    echo "Testing $test_name..."
    test_start_ms=$(now_ms)
    auxiliary_output=$(run_with_timeout "$timeout_seconds" "$@" 2>&1)
    auxiliary_exit=$?
    test_elapsed_ms=$(elapsed_ms_since "$test_start_ms")
    if [[ $auxiliary_exit -eq 0 ]]; then
        append_test_result "PASS" "$GREEN" "$test_name" "" "$test_elapsed_ms"
        ((passed++))
    else
        append_test_result "FAIL" "$RED" "$test_name" "exit $auxiliary_exit" "$test_elapsed_ms"
        [[ -n "$auxiliary_output" ]] && test_output+="  $auxiliary_output\n"
        ((failed++)) || true
        failures+=("$test_name")
    fi
}

run_auxiliary_test "dl_file_discovery" 10 python3 -B "$SCRIPT_DIR/test_dl_files.py"
run_auxiliary_test "diagnostic_expectations" 10 python3 -B "$SCRIPT_DIR/test_diagnostic_expectations.py"
run_auxiliary_test "dependency_installer" 10 python3 -B "$SCRIPT_DIR/test_install.py"
run_auxiliary_test "import_root_consistency" 60 python3 -B "$SCRIPT_DIR/test_import_roots.py" "$COMPILER"

echo "Testing timeout_process_tree..."
timeout_test_start_ms=$(now_ms)
timeout_test_output=$(run_with_timeout 1 python3 -B "$SCRIPT_DIR/test_timeout_process_tree.py" 2>&1)
timeout_test_exit=$?
timeout_test_elapsed_ms=$(elapsed_ms_since "$timeout_test_start_ms")
if [[ $timeout_test_exit -eq 124 && $timeout_test_elapsed_ms -lt 10000 ]]; then
    append_test_result "PASS" "$GREEN" "timeout_process_tree" "" "$timeout_test_elapsed_ms"
    ((passed++))
else
    append_test_result \
        "FAIL" "$RED" "timeout_process_tree" \
        "expected timeout exit 124 within 10000 ms, got exit $timeout_test_exit" "$timeout_test_elapsed_ms"
    [[ -n "$timeout_test_output" ]] && test_output+="  $timeout_test_output\n"
    ((failed++)) || true
    failures+=("timeout_process_tree")
fi

echo "Testing lsp_integration..."
lsp_test_start_ms=$(now_ms)
lsp_shell="$BASH"
if [[ "$is_windows" == "true" ]] && command -v cygpath >/dev/null 2>&1; then
    lsp_shell=$(cygpath -w "$lsp_shell")
fi
lsp_test_output=$(run_with_timeout 30 "$lsp_shell" "$SCRIPT_DIR/test_lsp.sh" 2>&1)
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
