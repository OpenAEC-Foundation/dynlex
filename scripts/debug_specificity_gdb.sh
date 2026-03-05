#!/bin/bash
set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "This helper is intended for macOS."
    exit 1
fi

if ! command -v gdb >/dev/null 2>&1; then
    echo "gdb not found. Install it with: brew install gdb"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

if ! codesign -dv "$(command -v gdb)" >/dev/null 2>&1; then
    cat <<'MSG'
gdb is not code-signed for debugging on macOS.
Follow docs/macos-gdb.md, then rerun this script.
MSG
    exit 1
fi

"$SCRIPT_DIR/build.sh" --lint=false

exec gdb --args "$PROJECT_DIR/build/dynlex" \
    "$PROJECT_DIR/tests/required/specificity/main.dl" \
    -o /tmp/specificity.out
