#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SERVER_PORT="${DYNLEX_TEST_SERVER_PORT:-8765}"
DEBUG_PORT="${DYNLEX_TEST_DEBUG_PORT:-9222}"

for dependency in node python3 google-chrome setsid; do
    if ! command -v "$dependency" >/dev/null 2>&1; then
        echo "Missing required dependency: $dependency"
        exit 1
    fi
done

BROWSER_PROFILE="$(mktemp -d)"
SERVER_LOG="$(mktemp)"
BROWSER_LOG="$(mktemp)"
SERVER_PID=""
BROWSER_PID=""

cleanup() {
    if [[ -n "$BROWSER_PID" ]]; then
        kill -- "-$BROWSER_PID" 2>/dev/null || true
        wait "$BROWSER_PID" 2>/dev/null || true
    fi
    if [[ -n "$SERVER_PID" ]]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    for _attempt in {1..20}; do
        if find "$BROWSER_PROFILE" -depth -delete 2>/dev/null; then
            break
        fi
        sleep 0.05
    done
    if [[ -e "$BROWSER_PROFILE" ]]; then
        echo "Failed to remove temporary browser profile: $BROWSER_PROFILE"
        return 1
    fi
    find "$SERVER_LOG" "$BROWSER_LOG" -maxdepth 0 -delete
}
trap cleanup EXIT

python3 -m http.server "$SERVER_PORT" --directory "$PROJECT_DIR/web" >"$SERVER_LOG" 2>&1 &
SERVER_PID="$!"
setsid google-chrome --headless=new --no-sandbox --enable-unsafe-swiftshader \
    --remote-debugging-port="$DEBUG_PORT" --user-data-dir="$BROWSER_PROFILE" \
    --window-size=1440,1000 about:blank >"$BROWSER_LOG" 2>&1 &
BROWSER_PID="$!"

READY=false
for _attempt in {1..100}; do
    if node -e "Promise.all([fetch('http://127.0.0.1:$SERVER_PORT/'), fetch('http://127.0.0.1:$DEBUG_PORT/json/list')]).then((responses) => { if (responses.some((response) => !response.ok)) process.exit(1); }).catch(() => process.exit(1));"; then
        READY=true
        break
    fi
    sleep 0.1
done

if [[ "$READY" != true ]]; then
    echo "Browser test services did not start"
    sed -n '1,120p' "$SERVER_LOG"
    sed -n '1,120p' "$BROWSER_LOG"
    exit 1
fi

DYNLEX_CDP_ORIGIN="http://127.0.0.1:$DEBUG_PORT" \
DYNLEX_SITE_ORIGIN="http://127.0.0.1:$SERVER_PORT" \
node "$PROJECT_DIR/tests/web/browser_execution.mjs"
