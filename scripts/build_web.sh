#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build-web"
WEB_COMPILER_DIR="$PROJECT_DIR/src/web/ide/public/compiler"

for dep in emcmake emcc cmake ninja; do
    if ! command -v "$dep" >/dev/null 2>&1; then
        echo "Missing required dependency: $dep"
        echo "Install Emscripten + CMake + Ninja first."
        exit 1
    fi
done

mkdir -p "$BUILD_DIR"

CMAKE_ARGS=(
    -S "$PROJECT_DIR"
    -B "$BUILD_DIR"
    -G Ninja
    -DCMAKE_BUILD_TYPE=Release
    -DDYNLEX_WEB=ON
)

if [ -n "${LLVM_DIR:-}" ]; then
    CMAKE_ARGS+=("-DLLVM_DIR=$LLVM_DIR")
fi
if [ -n "${DYNLEX_LLVM_VERSION:-}" ]; then
    CMAKE_ARGS+=("-DDYNLEX_LLVM_VERSION=$DYNLEX_LLVM_VERSION")
fi

emcmake cmake "${CMAKE_ARGS[@]}"
cmake --build "$BUILD_DIR" --target dynlex_web

mkdir -p "$WEB_COMPILER_DIR"
cp "$BUILD_DIR/dynlex_web.js" "$WEB_COMPILER_DIR/"
cp "$BUILD_DIR/dynlex_web.wasm" "$WEB_COMPILER_DIR/"
if [ -f "$BUILD_DIR/dynlex_web.data" ]; then
    cp "$BUILD_DIR/dynlex_web.data" "$WEB_COMPILER_DIR/"
fi

echo "Web compiler artifacts copied to: $WEB_COMPILER_DIR"
