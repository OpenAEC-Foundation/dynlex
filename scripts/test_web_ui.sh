#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
for source in src/web/compiler/*.js tests/web/*.mjs; do node --check "$source"; done
node tests/web/runtime_heap.mjs
node tests/web/graphics_backend.mjs
node scripts/generate_call_colors.mjs --check
node tests/web/ui_framework.mjs src/web/ide/public/compiler
node tests/web/project_sources.mjs src/web/ide/public/compiler
