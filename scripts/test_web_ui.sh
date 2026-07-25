#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

if ! command -v node >/dev/null 2>&1; then
    echo "Missing required dependency: node"
    exit 1
fi

node --check "$PROJECT_DIR/web/homepage.js"
node --check "$PROJECT_DIR/web/shader-banner.js"
node --check "$PROJECT_DIR/web/shader-renderer.js"
node --check "$PROJECT_DIR/web/snippet-highlights.js"
node --check "$PROJECT_DIR/web/snippet-highlight-key.js"
node --check "$PROJECT_DIR/web/semantic-highlighting.js"
node --check "$PROJECT_DIR/web/semantic-token-legend.js"
node --check "$PROJECT_DIR/src/web/ide/src/main.js"
node --check "$PROJECT_DIR/src/web/ide/public/compiler/compiler-worker.js"
node --check "$PROJECT_DIR/tests/web/browser_execution.mjs"
node "$PROJECT_DIR/scripts/generate_homepage_highlights.mjs" --check
node "$PROJECT_DIR/tests/web/semantic_highlighting.mjs"
node "$PROJECT_DIR/tests/web/homepage.mjs"
node "$PROJECT_DIR/tests/web/homepage_shaders.mjs"
node "$PROJECT_DIR/tests/web/ide_shell.mjs"
