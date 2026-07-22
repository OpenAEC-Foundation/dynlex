#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/llvm_version.sh"

LLVM_VERSION="$(dynlex_install_llvm_version)"
LLVM_RELEASE="${DYNLEX_LLVM_WASM_RELEASE:-$(dynlex_wasm_llvm_release)}"
EMSCRIPTEN_VERSION="${DYNLEX_EMSCRIPTEN_VERSION:-$(dynlex_emscripten_version)}"
EMSDK_ROOT="${DYNLEX_EMSDK_ROOT:-$HOME/emsdk}"
LLVM_ROOT="${DYNLEX_LLVM_WASM_ROOT:-$HOME/toolchains/llvm-wasm-$LLVM_VERSION}"
LLVM_SOURCE_DIR="$LLVM_ROOT/source"
LLVM_BUILD_DIR="$LLVM_ROOT/build"
LLVM_INSTALL_DIR="$LLVM_ROOT/install"
LLVM_CONFIG_DIR="$LLVM_INSTALL_DIR/lib/cmake/llvm"
VERSION_STAMP="$LLVM_ROOT/.dynlex-version"
BUILD_JOBS="${DYNLEX_LLVM_WASM_JOBS:-2}"
EXPECTED_VERSION_STAMP="LLVM $LLVM_RELEASE
Emscripten $EMSCRIPTEN_VERSION"

fail() {
    echo "Error: $*" >&2
    exit 1
}

require_command() {
    command -v "$1" >/dev/null 2>&1 || fail "required command '$1' was not found"
}

validate_configuration() {
    case "$LLVM_RELEASE" in
    "$LLVM_VERSION".*) ;;
    *) fail "LLVM release $LLVM_RELEASE does not match configured LLVM major version $LLVM_VERSION" ;;
    esac
    [[ "$BUILD_JOBS" =~ ^[1-9][0-9]*$ ]] || fail "DYNLEX_LLVM_WASM_JOBS must be a positive integer"
}

install_emscripten() {
    if [ ! -x "$EMSDK_ROOT/emsdk" ]; then
        if [ -e "$EMSDK_ROOT" ]; then
            fail "$EMSDK_ROOT exists but is not an Emscripten SDK checkout"
        fi
        mkdir -p "$(dirname "$EMSDK_ROOT")"
        git clone https://github.com/emscripten-core/emsdk.git "$EMSDK_ROOT"
    fi

    "$EMSDK_ROOT/emsdk" install "$EMSCRIPTEN_VERSION"
    "$EMSDK_ROOT/emsdk" activate "$EMSCRIPTEN_VERSION"
    export EMSDK_QUIET=1
    . "$EMSDK_ROOT/emsdk_env.sh"
    require_command emcc
    require_command emcmake
    emcc --check
}

native_llvm_tools() {
    local llvm_config
    llvm_config="$(dynlex_resolve_tool llvm-config "$LLVM_VERSION")"
    if ! command -v "$llvm_config" >/dev/null 2>&1 && [ "$(uname -s)" = "Darwin" ] && command -v brew >/dev/null 2>&1; then
        local brew_formula="llvm@$LLVM_VERSION"
        if ! brew list --formula --versions "$brew_formula" >/dev/null 2>&1; then
            brew_formula=llvm
        fi
        llvm_config="$(brew --prefix "$brew_formula")/bin/llvm-config"
    fi
    [ -x "$(command -v "$llvm_config" 2>/dev/null || printf '%s' "$llvm_config")" ] ||
        fail "matching native llvm-config for LLVM $LLVM_VERSION was not found"
    local installed_version
    installed_version="$("$llvm_config" --version)"
    [ "${installed_version%%.*}" = "$LLVM_VERSION" ] ||
        fail "$llvm_config reports LLVM $installed_version; expected major version $LLVM_VERSION"
    NATIVE_LLVM_BIN_DIR="$("$llvm_config" --bindir)"
    LLVM_TABLEGEN="$NATIVE_LLVM_BIN_DIR/llvm-tblgen"
    [ -x "$LLVM_TABLEGEN" ] || fail "matching native llvm-tblgen was not found at $LLVM_TABLEGEN"
}

installed_toolchain_is_current() {
    [ -f "$LLVM_CONFIG_DIR/LLVMConfig.cmake" ] || return 1
    [ -f "$VERSION_STAMP" ] || return 1
    [ "$(cat "$VERSION_STAMP")" = "$EXPECTED_VERSION_STAMP" ]
}

migrate_toolchain_version() {
    if [ ! -f "$VERSION_STAMP" ]; then
        if [ -e "$LLVM_SOURCE_DIR" ] || [ -e "$LLVM_BUILD_DIR" ] || [ -e "$LLVM_INSTALL_DIR" ]; then
            echo "Replacing unversioned WebAssembly LLVM toolchain state"
            rm -rf "$LLVM_SOURCE_DIR" "$LLVM_BUILD_DIR" "$LLVM_INSTALL_DIR"
        fi
        return
    fi
    local installed_stamp
    installed_stamp="$(cat "$VERSION_STAMP")"
    [ "$installed_stamp" = "$EXPECTED_VERSION_STAMP" ] && return

    echo "Replacing WebAssembly LLVM toolchain version:"
    printf '  installed: %s\n' "${installed_stamp//$'\n'/, }"
    printf '  requested: %s\n' "${EXPECTED_VERSION_STAMP//$'\n'/, }"
    rm -rf "$LLVM_BUILD_DIR" "$LLVM_INSTALL_DIR"
    if [ "${installed_stamp%%$'\n'*}" != "LLVM $LLVM_RELEASE" ]; then
        rm -rf "$LLVM_SOURCE_DIR"
    fi
    rm -f "$VERSION_STAMP"
}

prepare_llvm_source() {
    local expected_tag="llvmorg-$LLVM_RELEASE"
    if [ ! -d "$LLVM_SOURCE_DIR/.git" ]; then
        if [ -e "$LLVM_SOURCE_DIR" ]; then
            fail "$LLVM_SOURCE_DIR exists but is not the expected LLVM checkout"
        fi
        mkdir -p "$LLVM_ROOT"
        git clone --depth 1 --branch "$expected_tag" https://github.com/llvm/llvm-project.git "$LLVM_SOURCE_DIR"
        return
    fi

    local actual_tag
    actual_tag="$(git -C "$LLVM_SOURCE_DIR" describe --tags --exact-match 2>/dev/null || true)"
    [ "$actual_tag" = "$expected_tag" ] ||
        fail "$LLVM_SOURCE_DIR contains ${actual_tag:-an untagged revision}; expected $expected_tag"
}

build_llvm() {
    local cmake_arguments=(
        -S "$LLVM_SOURCE_DIR/llvm"
        -B "$LLVM_BUILD_DIR"
        -G Ninja
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_INSTALL_PREFIX="$LLVM_INSTALL_DIR"
        -DLLVM_TARGETS_TO_BUILD=WebAssembly
        -DLLVM_BUILD_TOOLS=OFF
        -DLLVM_BUILD_UTILS=OFF
        -DLLVM_INCLUDE_TESTS=OFF
        -DLLVM_INCLUDE_EXAMPLES=OFF
        -DLLVM_INCLUDE_BENCHMARKS=OFF
        -DLLVM_INCLUDE_DOCS=OFF
        -DLLVM_ENABLE_BINDINGS=OFF
        -DLLVM_ENABLE_FFI=OFF
        -DLLVM_ENABLE_LIBEDIT=OFF
        -DLLVM_ENABLE_LIBXML2=OFF
        -DLLVM_ENABLE_ZLIB=OFF
        -DLLVM_ENABLE_ZSTD=OFF
        -DLLVM_ENABLE_CURL=OFF
        -DLLVM_ENABLE_THREADS=OFF
        -DLLVM_ENABLE_UNWIND_TABLES=OFF
        -DLLVM_NATIVE_TOOL_DIR="$NATIVE_LLVM_BIN_DIR"
        -DLLVM_TABLEGEN="$LLVM_TABLEGEN"
    )
    emcmake cmake "${cmake_arguments[@]}"
    cmake --build "$LLVM_BUILD_DIR" --target install --parallel "$BUILD_JOBS"
    [ -f "$LLVM_CONFIG_DIR/LLVMConfig.cmake" ] || fail "LLVM installation did not produce LLVMConfig.cmake"
    printf '%s\n' "$EXPECTED_VERSION_STAMP" > "$VERSION_STAMP"
}

publish_environment() {
    if [ -n "${GITHUB_ENV:-}" ]; then
        printf 'LLVM_DIR=%s\n' "$LLVM_CONFIG_DIR" >> "$GITHUB_ENV"
        printf 'DYNLEX_LLVM_VERSION=%s\n' "$LLVM_VERSION" >> "$GITHUB_ENV"
    fi
    echo "Web compiler toolchain is ready."
    echo "For this shell, run:"
    echo "  source \"$EMSDK_ROOT/emsdk_env.sh\""
    echo "  export LLVM_DIR=\"$LLVM_CONFIG_DIR\""
    echo "  export DYNLEX_LLVM_VERSION=\"$LLVM_VERSION\""
}

main() {
    validate_configuration
    for command in git cmake ninja python3; do
        require_command "$command"
    done
    install_emscripten
    native_llvm_tools
    migrate_toolchain_version
    if ! installed_toolchain_is_current; then
        prepare_llvm_source
        build_llvm
    fi
    publish_environment
}

main "$@"
