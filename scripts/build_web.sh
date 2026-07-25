#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build-web"
WEB_COMPILER_DIR="$PROJECT_DIR/src/web/ide/public/compiler"
. "$SCRIPT_DIR/llvm_toolchain.sh"

for dep in emcmake emcc cmake ninja git; do
    if ! command -v "$dep" >/dev/null 2>&1; then
        echo "Missing required dependency: $dep"
        echo "Install Emscripten + CMake + Ninja first."
        exit 1
    fi
done

dynlex_ensure_llvm_toolchain web
mkdir -p "$BUILD_DIR"

CMAKE_ARGS=(
    -S "$PROJECT_DIR"
    -B "$BUILD_DIR"
    -G Ninja
    -U DYNLEX_LLVM_VERSION
    -DCMAKE_BUILD_TYPE=Release
    -DDYNLEX_WEB=ON
    -DLLVM_DIR="$DYNLEX_LLVM_WEB_INSTALL_DIR/lib/cmake/llvm"
)

emcmake cmake "${CMAKE_ARGS[@]}"
cmake --build "$BUILD_DIR" --target dynlex_web

mkdir -p "$WEB_COMPILER_DIR"
cp "$BUILD_DIR/dynlex_web.js" "$WEB_COMPILER_DIR/"
cp "$BUILD_DIR/dynlex_web.wasm" "$WEB_COMPILER_DIR/"
if [ -f "$BUILD_DIR/dynlex_web.data" ]; then
    cp "$BUILD_DIR/dynlex_web.data" "$WEB_COMPILER_DIR/"
fi

echo "Web compiler artifacts copied to: $WEB_COMPILER_DIR"
