#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
COMPILER="$PROJECT_DIR/build/dynlex"
if [[ ! -x "$COMPILER" && -x "$PROJECT_DIR/build/dynlex.exe" ]]; then
    COMPILER="$PROJECT_DIR/build/dynlex.exe"
fi

SOURCE_FILE="$PROJECT_DIR/tests/purity_dump/main.dl"
EXPECTED_FILE="$PROJECT_DIR/tests/purity_dump/expected.txt"
LLVM_OUTPUT="$(mktemp "${TMPDIR:-/tmp}/dynlex-purity-dump.XXXXXX.ll")"
trap 'rm -f "$LLVM_OUTPUT"' EXIT

actual_output=$("$COMPILER" --dump-purity --emit-llvm "$SOURCE_FILE" -o "$LLVM_OUTPUT")
expected_output=$(<"$EXPECTED_FILE")

if [[ "$actual_output" != "$expected_output" ]]; then
    echo "Expected:"
    printf "%s\n" "$expected_output"
    echo
    echo "Actual:"
    printf "%s\n" "$actual_output"
    exit 1
fi
