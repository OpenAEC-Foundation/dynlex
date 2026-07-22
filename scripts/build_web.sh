#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build-web"
WEB_COMPILER_DIR="$PROJECT_DIR/src/web/ide/public/compiler"
. "$SCRIPT_DIR/llvm_version.sh"

LLVM_VERSION="$(dynlex_install_llvm_version)"
EMSCRIPTEN_VERSION="${DYNLEX_EMSCRIPTEN_VERSION:-$(dynlex_emscripten_version)}"
EMSDK_ROOT_WAS_CONFIGURED=false
if [ -n "${DYNLEX_EMSDK_ROOT+x}" ]; then
    EMSDK_ROOT_WAS_CONFIGURED=true
fi
EMSDK_ROOT="${DYNLEX_EMSDK_ROOT:-$HOME/emsdk}"
LLVM_WASM_ROOT="${DYNLEX_LLVM_WASM_ROOT:-$HOME/toolchains/llvm-wasm-$LLVM_VERSION}"
if [ -z "${LLVM_DIR:-}" ]; then
    LLVM_DIR="$LLVM_WASM_ROOT/install/lib/cmake/llvm"
fi
if [ ! -f "$LLVM_DIR/LLVMConfig.cmake" ]; then
    echo "Missing WebAssembly LLVM package: $LLVM_DIR/LLVMConfig.cmake"
    echo "Install it with: ./scripts/install.sh --web"
    exit 1
fi
export LLVM_DIR
export DYNLEX_LLVM_VERSION="$LLVM_VERSION"

if [ -f "$EMSDK_ROOT/emsdk_env.sh" ]; then
    export EMSDK_QUIET=1
    . "$EMSDK_ROOT/emsdk_env.sh"
elif [ "$EMSDK_ROOT_WAS_CONFIGURED" = true ] || ! command -v emcmake >/dev/null 2>&1 || ! command -v emcc >/dev/null 2>&1; then
        echo "Missing Emscripten SDK environment: $EMSDK_ROOT/emsdk_env.sh"
        echo "Install it with: ./scripts/install.sh --web"
        exit 1
fi

INSTALLED_EMSCRIPTEN_VERSION="$(emcc --version | sed -n '1 s/.*) \([0-9][^ ]*\) .*/\1/p')"
if [ "$INSTALLED_EMSCRIPTEN_VERSION" != "$EMSCRIPTEN_VERSION" ]; then
    echo "Emscripten version $INSTALLED_EMSCRIPTEN_VERSION is active; expected $EMSCRIPTEN_VERSION"
    echo "Install and activate it with: ./scripts/install.sh --web"
    exit 1
fi

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

CMAKE_ARGS+=("-DLLVM_DIR=$LLVM_DIR")
CMAKE_ARGS+=("-DDYNLEX_LLVM_VERSION=$DYNLEX_LLVM_VERSION")

emcmake cmake "${CMAKE_ARGS[@]}"
cmake --build "$BUILD_DIR" --target dynlex_web

mkdir -p "$WEB_COMPILER_DIR"
cp "$BUILD_DIR/dynlex_web.js" "$WEB_COMPILER_DIR/"
cp "$BUILD_DIR/dynlex_web.wasm" "$WEB_COMPILER_DIR/"
if [ -f "$BUILD_DIR/dynlex_web.data" ]; then
    cp "$BUILD_DIR/dynlex_web.data" "$WEB_COMPILER_DIR/"
fi

echo "Web compiler artifacts copied to: $WEB_COMPILER_DIR"
