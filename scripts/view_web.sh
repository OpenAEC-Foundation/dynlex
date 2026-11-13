#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
WEB_ROOT_DIR="${DYNLEX_WEB_ROOT:-$PROJECT_DIR/build/site}"
HOST="${DYNLEX_WEB_HOST:-127.0.0.1}"
PORT="${1:-8000}"
URL="http://$HOST:$PORT/index.html"

if ! command -v python3 >/dev/null 2>&1; then
    echo "Missing required dependency: python3"
    exit 1
fi

if [ ! -f "$WEB_ROOT_DIR/index.html" ]; then
    echo "Missing web root entry: $WEB_ROOT_DIR/index.html"
    echo "Run python3 $PROJECT_DIR/scripts/build_site.py first."
    exit 1
fi

WEB_ROOT_DIR="$(cd "$WEB_ROOT_DIR" && pwd)"
echo "Serving $WEB_ROOT_DIR at $URL"
cd "$PROJECT_DIR"
exec python3 "$SCRIPT_DIR/serve_web.py" "$PORT" --bind "$HOST" --directory "$WEB_ROOT_DIR"
