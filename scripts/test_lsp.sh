#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "Running TCP startup failure test..."
python3 "$SCRIPT_DIR/test_lsp_tcp_startup.py"
echo "Running concurrent stdio test..."
python3 "$SCRIPT_DIR/test_lsp_concurrent_stdio.py"
echo "Running cursor commit test..."
python3 "$SCRIPT_DIR/test_lsp_cursor_commit.py"
