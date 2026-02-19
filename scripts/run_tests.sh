#!/bin/bash
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
COMPILER="$PROJECT_DIR/build/dynlex"
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

# Known failing tests — these don't count as unexpected failures
KNOWN_FAILURES=""

if [[ ! -x "$COMPILER" ]]; then
    echo -e "${YELLOW}Compiler not found, building...${NC}"
    "$SCRIPT_DIR/build.sh"
fi

for test_dir in "$TESTS_DIR"/*/; do
    test_dir="${test_dir%/}"
    test_name="$(basename "$test_dir")"
    source_file="$test_dir/main.dl"
    expected_file="$test_dir/expected.txt"
    output_binary="$test_dir/main.out"

    if [[ ! -f "$source_file" ]]; then
        test_output+="${YELLOW}SKIP${NC} $test_name (no main.dl)\n"
        ((skipped++))
        continue
    fi

    expected_error_file="$test_dir/expected_error.txt"

    if [[ ! -f "$expected_file" && ! -f "$expected_error_file" ]]; then
        test_output+="${YELLOW}SKIP${NC} $test_name (no expected.txt or expected_error.txt)\n"
        ((skipped++))
        continue
    fi

    # Compile (5 second timeout)
    compile_output=$(timeout 5 "$COMPILER" "$source_file" -o "$output_binary" 2>&1)
    compile_exit=$?
    if [[ $compile_exit -eq 124 ]]; then
        test_output+="${RED}FAIL${NC} $test_name (compilation timed out)\n"
        ((failed++))
        failures+=("$test_name")
        continue
    fi
    if [[ $compile_exit -ne 0 || ! -f "$output_binary" || ! -x "$output_binary" ]]; then
        # Compilation failed — check if this was expected
        if [[ -f "$expected_error_file" ]]; then
            expected_error=$(<"$expected_error_file")
            if [[ "$compile_output" == *"$expected_error"* ]]; then
                test_output+="${GREEN}PASS${NC} $test_name\n"
                ((passed++))
            else
                test_output+="${RED}FAIL${NC} $test_name (compile error mismatch)\n"
                test_output+="  Expected error containing: $expected_error\n"
                test_output+="  Actual: $compile_output\n"
                ((failed++))
                failures+=("$test_name")
            fi
        else
            test_output+="${RED}FAIL${NC} $test_name (compilation failed)\n"
            [[ -n "$compile_output" ]] && test_output+="  $compile_output\n"
            ((failed++)) || true
            failures+=("$test_name")
        fi
        continue
    fi

    # Compilation succeeded but we expected an error
    if [[ -f "$expected_error_file" && ! -f "$expected_file" ]]; then
        test_output+="${RED}FAIL${NC} $test_name (expected compile error but compilation succeeded)\n"
        ((failed++))
        failures+=("$test_name")
        continue
    fi

    # Run (5 second timeout)
    actual_output=$(timeout 5 "$output_binary" 2>&1)
    run_exit=$?
    if [[ $run_exit -eq 124 ]]; then
        test_output+="${RED}FAIL${NC} $test_name (execution timed out)\n"
        ((failed++))
        failures+=("$test_name")
        continue
    fi
    if [[ $run_exit -ne 0 ]]; then
        test_output+="${RED}FAIL${NC} $test_name (runtime error, exit $run_exit)\n"
        test_output+="  $actual_output\n"
        ((failed++))
        failures+=("$test_name")
        continue
    fi

    # Compare
    expected_output=$(<"$expected_file")
    if [[ "$actual_output" == "$expected_output" ]]; then
        test_output+="${GREEN}PASS${NC} $test_name\n"
        ((passed++))
    else
        test_output+="${RED}FAIL${NC} $test_name (output mismatch)\n"
        test_output+="  Expected: $(head -c 200 <<< "$expected_output")\n"
        test_output+="  Actual:   $(head -c 200 <<< "$actual_output")\n"
        ((failed++))
        failures+=("$test_name")
    fi
done

# Only show per-test details if there are failures
if [[ $failed -gt 0 ]]; then
    echo -e "$test_output"
fi

echo "Results: ${passed} passed, ${failed} failed, ${skipped} skipped"

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
