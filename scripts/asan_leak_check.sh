#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SUPPRESSIONS_FILE="$SCRIPT_DIR/lsan.supp"
BUILD_DIR="${DYNLEX_ASAN_BUILD_DIR:-$PROJECT_DIR/build-asan}"
COMPILER="$BUILD_DIR/dynlex"

if [[ ! -x "$COMPILER" ]]; then
    echo "ASan compiler not found at: $COMPILER"
    echo "Build it first, for example:"
    echo "  cmake -S . -B build-asan -G Ninja \\"
    echo "    -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \\"
    echo "    -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' && cmake --build build-asan -j"
    exit 1
fi

if [[ ! -f "$SUPPRESSIONS_FILE" ]]; then
    echo "Missing suppression file: $SUPPRESSIONS_FILE"
    exit 1
fi

if [[ $# -eq 0 ]]; then
    set -- "$PROJECT_DIR/tests/required/precedence/main.dl" -o /tmp/dynlex_asan_leak_check.out
fi

LSAN_BASE="suppressions=$SUPPRESSIONS_FILE:print_suppressions=1"
ASAN_BASE="detect_leaks=1:exitcode=23:halt_on_error=0"
export LSAN_OPTIONS="${LSAN_OPTIONS:+$LSAN_OPTIONS:}$LSAN_BASE"
export ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS:}$ASAN_BASE"

"$COMPILER" "$@"
