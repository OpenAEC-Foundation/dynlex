#!/bin/bash

# Run all required tests
# Usage: ./run_tests.sh

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TESTS_DIR="$SCRIPT_DIR/tests/required"

failed=0
passed=0

for test_dir in "$TESTS_DIR"/*/; do
    test_file="$test_dir/main.3bx"
    test_name="$(basename "$test_dir")"

    if [ ! -f "$test_file" ]; then
        continue
    fi

    echo "=== Test: $test_name ==="
    if "$SCRIPT_DIR/compile_and_run.sh" "$test_file"; then
        echo "PASSED: $test_name"
        ((passed++))
    else
        echo "FAILED: $test_name"
        ((failed++))
    fi
    echo ""
done

echo "=== Results ==="
echo "Passed: $passed"
echo "Failed: $failed"

if [ $failed -gt 0 ]; then
    exit 1
fi
