#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

node "$SCRIPT_DIR/generate_call_colors.mjs" --check
python3 "$SCRIPT_DIR/test_lsp_call_colors.py"
python3 "$SCRIPT_DIR/test_lsp_unicode.py"
python3 "$SCRIPT_DIR/test_lsp_symbols.py"

echo "Running TCP startup failure test..."
python3 "$SCRIPT_DIR/test_lsp_tcp_startup.py"
echo "Running concurrent stdio test..."
python3 "$SCRIPT_DIR/test_lsp_concurrent_stdio.py"
echo "Running config document test..."
python3 "$SCRIPT_DIR/test_lsp_config.py"
echo "Running declaration shorthand token test..."
python3 "$SCRIPT_DIR/test_lsp_declaration_shorthands.py"
echo "Running cursor commit test..."
python3 "$SCRIPT_DIR/test_lsp_cursor_commit.py"
echo "Running instantiation label test..."
python3 "$SCRIPT_DIR/test_lsp_instantiation_labels.py"
echo "Running call-expression range test..."
python3 "$SCRIPT_DIR/test_lsp_call_expressions.py"
echo "Running completion frontier test..."
python3 "$SCRIPT_DIR/test_lsp_completions.py"
python3 "$SCRIPT_DIR/test_lsp_nested_completions.py"
echo "Running return type quick-fix test..."
python3 "$SCRIPT_DIR/test_lsp_return_type_quick_fix.py"
echo "Running function pattern semantic-stage test..."
python3 "$SCRIPT_DIR/test_lsp_function_pattern_stage.py"
