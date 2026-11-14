#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
COMPILER="$PROJECT_DIR/build/dynlex"
WEB_ROOT="${DYNLEX_TEST_WEB_ROOT:-$PROJECT_DIR/build/site}"
for dependency in node python3; do
    if ! command -v "$dependency" >/dev/null 2>&1; then
        echo "Missing required dependency: $dependency"
        exit 1
    fi
done
cd "$PROJECT_DIR"
for source in src/web/compiler/*.js site/platform/*.js tests/web/*.mjs; do
    node --check "$source"
done
python3 tests/web/source_package.py
python3 tests/web/site_routes.py
python3 tests/web/site_preview.py
python3 tests/web/site_launchers.py
node tests/web/graphics_backend.mjs
node tests/web/firebase_provider.mjs
node scripts/generate_call_colors.mjs --check
python3 scripts/generate_farm_vocabulary.py --check
node tests/web/ui_framework.mjs src/web/ide/public/compiler
node tests/web/project_sources.mjs src/web/ide/public/compiler
node tests/web/site_models.mjs
python3 tests/web/site_runtime.py
node tests/web/farm_models.mjs "$COMPILER" --native-wasm --shipped
python3 tests/web/farm_art.py --compiler "$COMPILER"
python3 tests/web/farm_art.py --compiler "$COMPILER" --case crops
node tests/web/progress_model.mjs
node tests/web/library_values.mjs "$COMPILER" --shipped
node tests/web/site_games.mjs
node tests/web/site_program_protocol.mjs
node tests/web/site_deployment.mjs "$WEB_ROOT"
node tests/release/distribution.mjs
