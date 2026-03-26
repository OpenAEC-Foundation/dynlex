#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

if ! command -v node >/dev/null 2>&1; then
    echo "Missing required dependency: node"
    exit 1
fi

"$SCRIPT_DIR/build_web.sh"
node "$PROJECT_DIR/tests/web/smoke.mjs" "$PROJECT_DIR/build-web"
