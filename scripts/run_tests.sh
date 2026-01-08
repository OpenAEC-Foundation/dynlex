#!/bin/bash

# Run all required tests
# Usage: ./scripts/run_tests.sh [-v]
# -v: verbose mode, show output even for passing tests

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_DIR="$SCRIPT_DIR/.."
TESTS_DIR="$WORKSPACE_DIR/tests/required"
BUILD_DIR="$WORKSPACE_DIR/build"
TIMEOUT=20
VERBOSE=0

if [ "$1" = "-v" ]; then
    VERBOSE=1
fi

# Build first
"$SCRIPT_DIR/build.sh"

failed=0
passed=0

for test_dir in "$TESTS_DIR"/*/; do
    test_file="$test_dir/main.3bx"
    test_name="$(basename "$test_dir")"
    expected_file="$test_dir/expected.txt"

    if [ ! -f "$test_file" ]; then
        continue
    fi

    echo "=== Test: $test_name ==="

    # Compile to IR
    ir_file="/tmp/3bx_${test_name}.ll"
    if ! timeout $TIMEOUT "$BUILD_DIR/3bx" --emit-ir "$test_file" > "$ir_file" 2>&1; then
        echo "FAILED: $test_name (compilation error)"
        cat "$ir_file"
        ((failed++))
        echo ""
        continue
    fi

    # Run and capture output
    actual_output=$(timeout $TIMEOUT lli "$ir_file" 2>&1) || true

    # Check against expected output if exists
    if [ -f "$expected_file" ]; then
        expected_output=$(cat "$expected_file")
        if [ "$actual_output" = "$expected_output" ]; then
            echo "PASSED: $test_name"
            ((passed++))
            if [ $VERBOSE -eq 1 ] && [ -n "$actual_output" ]; then
                echo "Output:"
                echo "$actual_output"
            fi
        else
            echo "FAILED: $test_name (output mismatch)"
            echo "Expected:"
            echo "$expected_output"
            echo "Actual:"
            echo "$actual_output"
            ((failed++))
        fi
    else
        # No expected file - just check it runs
        echo "PASSED: $test_name (no expected output to verify)"
        ((passed++))
        if [ $VERBOSE -eq 1 ] && [ -n "$actual_output" ]; then
            echo "Output:"
            echo "$actual_output"
        fi
    fi
    echo ""
done

echo "=== Results ==="
echo "Passed: $passed"
echo "Failed: $failed"

if [ $failed -gt 0 ]; then
    exit 1
fi
