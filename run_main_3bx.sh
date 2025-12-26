#!/bin/bash

# Run tests/active/main.3bx
# Usage: ./run_main_3bx.sh

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
"$SCRIPT_DIR/compile_and_run.sh" "$SCRIPT_DIR/tests/active/main.3bx"
