#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
: "${DYNLEX_BROWSER_TEST_ENTRY:?Select a browser test entry with DYNLEX_BROWSER_TEST_ENTRY}"
SERVER_PORT="${DYNLEX_TEST_SERVER_PORT:-8765}"
DEBUG_PORT="${DYNLEX_TEST_DEBUG_PORT:-9222}"
WEB_SERVER="${DYNLEX_TEST_WEB_SERVER:-static}"
BROWSER_TEST_ENTRY="$DYNLEX_BROWSER_TEST_ENTRY"

GRAPHICS_BACKEND="${DYNLEX_TEST_GRAPHICS:-webgpu}"
dependencies=(node python3 google-chrome setsid)
if [[ "$GRAPHICS_BACKEND" = webgpu ]]; then
    dependencies+=(xvfb-run Xvfb)
fi
for dependency in "${dependencies[@]}"; do
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

if [[ "$WEB_SERVER" != static ]]; then
    echo "Unknown compiler browser test server: $WEB_SERVER" >&2
    exit 1
fi
python3 -m http.server "$SERVER_PORT" --bind 127.0.0.1 \
    --directory "${DYNLEX_TEST_WEB_ROOT:?Select the fixture directory}" >"$SERVER_LOG" 2>&1 &
SERVER_PID="$!"
if [[ "$GRAPHICS_BACKEND" = webgpu ]]; then
    # Exercise both browser APIs on one software driver, including the farm's WebGL2 renderer.
    # https://chromium.googlesource.com/chromium/src/+/HEAD/docs/gpu/swiftshader.md
    setsid env -u WAYLAND_DISPLAY -u WAYLAND_SOCKET -u VK_DRIVER_FILES -u VK_ICD_FILENAMES \
        XDG_SESSION_TYPE=x11 xvfb-run -a -s "-screen 0 1440x1000x24" \
        google-chrome --no-sandbox --no-first-run --no-default-browser-check --mute-audio \
        --disable-background-networking --enable-unsafe-webgpu \
        --enable-features=Vulkan,WebGPU --use-gl=angle --use-angle=swiftshader \
        --use-vulkan=swiftshader --use-webgpu-adapter=swiftshader --disable-vulkan-surface \
        --remote-debugging-port="$DEBUG_PORT" --user-data-dir="$BROWSER_PROFILE" \
        --window-size=1440,1000 about:blank >"$BROWSER_LOG" 2>&1 &
elif [[ "$GRAPHICS_BACKEND" = webgl ]]; then
    setsid env -u WAYLAND_DISPLAY -u WAYLAND_SOCKET \
        google-chrome --headless=new --no-sandbox --use-gl=angle --use-angle=swiftshader --mute-audio \
        --no-first-run --no-default-browser-check --disable-background-networking \
        --remote-debugging-port="$DEBUG_PORT" --user-data-dir="$BROWSER_PROFILE" \
        --window-size=1440,1000 about:blank >"$BROWSER_LOG" 2>&1 &
else
    echo "Unknown browser graphics backend: $GRAPHICS_BACKEND" >&2
    exit 1
fi
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
