#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
WEB_ROOT_DIR="$PROJECT_DIR/web"
HOST="${DYNLEX_WEB_HOST:-127.0.0.1}"
PORT="${1:-8000}"
URL="http://$HOST:$PORT/index.html"

if ! command -v python3 >/dev/null 2>&1; then
    echo "Missing required dependency: python3"
    exit 1
fi

if [ ! -f "$WEB_ROOT_DIR/index.html" ]; then
    echo "Missing web root entry: $WEB_ROOT_DIR/index.html"
    echo "Run ./scripts/build_web_root.sh first."
    exit 1
fi

open_browser() {
    if command -v xdg-open >/dev/null 2>&1; then
        xdg-open "$URL" >/dev/null 2>&1 || true
        return
    fi
    if command -v open >/dev/null 2>&1; then
        open "$URL" >/dev/null 2>&1 || true
        return
    fi
    if command -v wslview >/dev/null 2>&1; then
        wslview "$URL" >/dev/null 2>&1 || true
        return
    fi
    if command -v powershell.exe >/dev/null 2>&1; then
        powershell.exe -NoProfile -Command "Start-Process '$URL'" >/dev/null 2>&1 || true
        return
    fi
    echo "Open this URL manually: $URL"
}

echo "Serving $WEB_ROOT_DIR at $URL"
(
    cd "$WEB_ROOT_DIR"
    python3 -m http.server "$PORT" --bind "$HOST"
) &
SERVER_PID=$!

cleanup() {
    kill "$SERVER_PID" >/dev/null 2>&1 || true
}
trap cleanup EXIT INT TERM

sleep 0.6
if ! kill -0 "$SERVER_PID" >/dev/null 2>&1; then
    echo "Failed to start local web server on $HOST:$PORT"
    exit 1
fi

open_browser
wait "$SERVER_PID"
