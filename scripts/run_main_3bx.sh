#!/bin/bash

# Run tests/active/main.3bx
# Usage: ./scripts/run_main_3bx.sh

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_DIR="$SCRIPT_DIR/.."
"$SCRIPT_DIR/compile_and_run.sh" "$WORKSPACE_DIR/tests/active/main.3bx"
