#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
if [[ -z "${DYNLEX_TEST_WEB_SERVER:-}" && -z "${DYNLEX_BROWSER_TEST_ENTRY:-}" ]]; then
    DYNLEX_TEST_WEB_SERVER=static "$0"
    DYNLEX_TEST_WEB_SERVER=vite \
        DYNLEX_TEST_SERVER_PORT="${DYNLEX_TEST_VITE_PORT:-8766}" \
        DYNLEX_BROWSER_TEST_ENTRY="$PROJECT_DIR/tests/web/vite_dev_browser.mjs" \
        "$0"
    exit 0
fi
SERVER_PORT="${DYNLEX_TEST_SERVER_PORT:-8765}"
DEBUG_PORT="${DYNLEX_TEST_DEBUG_PORT:-9222}"
WEB_SERVER="${DYNLEX_TEST_WEB_SERVER:-static}"
if [[ "$WEB_SERVER" == "vite" ]]; then
    BROWSER_TEST_ENTRY="${DYNLEX_BROWSER_TEST_ENTRY:-$PROJECT_DIR/tests/web/vite_dev_browser.mjs}"
else
    BROWSER_TEST_ENTRY="${DYNLEX_BROWSER_TEST_ENTRY:-$PROJECT_DIR/tests/web/browser_execution.mjs}"
fi

dependencies=(node python3 google-chrome setsid xvfb-run Xvfb)
if [[ "$WEB_SERVER" == "vite" ]]; then
    dependencies+=(npm)
fi
for dependency in "${dependencies[@]}"; do
    if ! command -v "$dependency" >/dev/null 2>&1; then
        echo "Missing required dependency: $dependency"
        exit 1
    fi
done
VULKAN_ICD="$(python3 "$SCRIPT_DIR/find_vulkan_icd.py")"
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

if [[ "$WEB_SERVER" == "static" ]]; then
    python3 -m http.server "$SERVER_PORT" --directory "$PROJECT_DIR/web" >"$SERVER_LOG" 2>&1 &
elif [[ "$WEB_SERVER" == "vite" ]]; then
    (cd "$PROJECT_DIR/src/web/ide" && npm run dev -- --host 127.0.0.1 --port "$SERVER_PORT") >"$SERVER_LOG" 2>&1 &
else
    echo "Unknown web test server: $WEB_SERVER" >&2
    exit 1
fi
SERVER_PID="$!"
VK_DRIVER_FILES="$VULKAN_ICD" VK_ICD_FILENAMES="$VULKAN_ICD" setsid xvfb-run -a -s "-screen 0 1440x1000x24" \
    google-chrome --no-sandbox --no-first-run --no-default-browser-check \
    --disable-background-networking --enable-unsafe-webgpu \
    --enable-features=Vulkan,WebGPU --use-vulkan=native --use-angle=vulkan \
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
node "$BROWSER_TEST_ENTRY"
