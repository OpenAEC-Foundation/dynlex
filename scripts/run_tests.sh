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
        echo -e "${YELLOW}SKIP${NC} $test_name (no main.dl)"
        ((skipped++))
        continue
    fi

    if [[ ! -f "$expected_file" ]]; then
        echo -e "${YELLOW}SKIP${NC} $test_name (no expected.txt)"
        ((skipped++))
        continue
    fi

    # Compile
    compile_output=$("$COMPILER" "$source_file" -o "$output_binary" 2>&1)
    if [[ $? -ne 0 || ! -x "$output_binary" ]]; then
        echo -e "${RED}FAIL${NC} $test_name (compilation failed)"
        [[ -n "$compile_output" ]] && echo "  $compile_output"
        ((failed++)) || true
        failures+=("$test_name")
        continue
    fi

    # Run
    if ! actual_output=$("$output_binary" 2>&1); then
        echo -e "${RED}FAIL${NC} $test_name (runtime error)"
        echo "  $actual_output"
        ((failed++))
        failures+=("$test_name")
        continue
    fi

    # Compare
    expected_output=$(<"$expected_file")
    if [[ "$actual_output" == "$expected_output" ]]; then
        echo -e "${GREEN}PASS${NC} $test_name"
        ((passed++))
    else
        echo -e "${RED}FAIL${NC} $test_name (output mismatch)"
        echo "  Expected: $(head -c 200 <<< "$expected_output")"
        echo "  Actual:   $(head -c 200 <<< "$actual_output")"
        ((failed++))
        failures+=("$test_name")
    fi
done

echo ""
echo "Results: ${passed} passed, ${failed} failed, ${skipped} skipped"

if [[ ${#failures[@]} -gt 0 ]]; then
    echo -e "${RED}Failed tests: ${failures[*]}${NC}"
    exit 1
fi
