#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

python3 "$SCRIPT_DIR/test_lsp_tcp_startup.py"
python3 "$SCRIPT_DIR/test_lsp_concurrent_stdio.py"
python3 "$SCRIPT_DIR/test_lsp_cursor_commit.py"
