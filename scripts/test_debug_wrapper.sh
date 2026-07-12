#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
DEBUG_SCRIPT="$PROJECT_DIR/scripts/debug.sh"

if ! command -v gdb >/dev/null 2>&1; then
    echo "gdb not found"
    exit 1
fi

if [[ ! -x "$DEBUG_SCRIPT" ]]; then
    echo "scripts/debug.sh not found or not executable"
    exit 1
fi

if [[ ! -x "$PROJECT_DIR/build/dynlex" && ! -x "$PROJECT_DIR/build/dynlex.exe" ]]; then
    echo "build/dynlex not found. Run ./scripts/build.sh first."
    exit 1
fi

output="$(
    cd /tmp
    "$DEBUG_SCRIPT" --gdb --batch -ex "python import gdb; print(any(getattr(printer, 'name', None) == 'dynlex' for printer in gdb.pretty_printers) or any(getattr(printer, 'name', None) == 'dynlex' for objfile in gdb.objfiles() for printer in objfile.pretty_printers))" -- 2>&1
)"

if [[ "$output" != *$'\nTrue'* && "$output" != True* && "$output" != *$'\nTrue\n'* ]]; then
    echo "DynLex pretty-printer was not loaded by scripts/debug.sh"
    printf '%s\n' "$output"
    exit 1
fi

echo "scripts/debug.sh loaded the DynLex pretty-printer"

match_progress_output="$(
    cd /tmp
    "$DEBUG_SCRIPT" --gdb --batch \
        -ex "break MatchProgress::step" \
        -ex "run $PROJECT_DIR/tests/required/memoized_pattern_matching/main.dl" \
        -ex "print *this" \
        -- 2>&1
)"

if [[ "$match_progress_output" != *"MatchProgress in-progress:"* ]]; then
    echo "DynLex MatchProgress pretty-printer did not render a live matcher state"
    printf '%s\n' "$match_progress_output"
    exit 1
fi

echo "scripts/debug.sh rendered a live MatchProgress state"
