#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/llvm_version.sh"

LLVM_VERSION="$(dynlex_detect_installed_llvm_version || true)"

if [ -z "$LLVM_VERSION" ]; then
    echo "Error: DynLex requires LLVM/Clang 20 or newer."
    echo "Set DYNLEX_LLVM_VERSION to an installed toolchain version or run: ./scripts/install.sh"
    exit 1
fi

CLANG="$(dynlex_resolve_tool clang "$LLVM_VERSION")"
CLANGXX="$(dynlex_resolve_tool clang++ "$LLVM_VERSION")"
CLANG_FORMAT="$(dynlex_resolve_tool clang-format "$LLVM_VERSION")"
CLANG_TIDY="$(dynlex_resolve_tool clang-tidy "$LLVM_VERSION")"

# Parse arguments
LINT=true
BUILD_TYPE=Debug
for arg in "$@"; do
    case $arg in
        --lint=false) LINT=false ;;
        --lint=true) LINT=true ;;
        --release) BUILD_TYPE=Release; LINT=false ;;
    esac
done

# Check for required dependencies
MISSING_DEPS=()

command -v "$CLANG" >/dev/null 2>&1 || MISSING_DEPS+=("$CLANG")
command -v cmake >/dev/null 2>&1 || MISSING_DEPS+=("cmake")
command -v ninja >/dev/null 2>&1 || MISSING_DEPS+=("ninja")
if [ "$LINT" = "true" ]; then
    command -v "$CLANG_FORMAT" >/dev/null 2>&1 || MISSING_DEPS+=("$CLANG_FORMAT")
    command -v "$CLANG_TIDY" >/dev/null 2>&1 || MISSING_DEPS+=("$CLANG_TIDY")
fi

if [ ${#MISSING_DEPS[@]} -ne 0 ]; then
    echo "Error: Missing required dependencies: ${MISSING_DEPS[*]}"
    echo "Please install missing dependencies manually or run: ./scripts/install.sh"
    exit 1
fi

# Use the newest supported clang toolchain that is installed.
export CC="$CLANG"
export CXX="$CLANGXX"

if [ "$LINT" = "true" ]; then
    # Format source files
    find src -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name '*.inl' | xargs "$CLANG_FORMAT" -i
fi

mkdir -p build
if [ -f build/CMakeCache.txt ] && grep -q "conan_toolchain\.cmake" build/CMakeCache.txt; then
    echo "Removing stale Conan-generated CMake cache..."
    rm -f build/CMakeCache.txt
    rm -rf build/CMakeFiles
fi

if [ -d build/CMakeFiles ] && grep -R -q "conan_toolchain\.cmake" build/CMakeFiles; then
    echo "Removing stale Conan-generated CMake metadata..."
    rm -f build/CMakeCache.txt
    rm -rf build/CMakeFiles
fi

if [ -f build/conan_toolchain.cmake ]; then
    echo "Removing stale Conan-generated package files..."
    rm -f build/CMakeCache.txt
    rm -rf build/CMakeFiles
    rm -f \
        build/CMakePresets.json \
        build/cmakedeps_flexes.cmake \
        build/conan_toolchain.cmake \
        build/conanbuild.sh \
        build/conanbuildenv-*.sh \
        build/conandeps_legacy.cmake \
        build/conanrun.sh \
        build/conanrunenv-*.sh \
        build/deactivate_conanbuild.sh \
        build/deactivate_conanrun.sh \
        build/nlohmann_json-Target-*.cmake \
        build/nlohmann_json-*.cmake \
        build/nlohmann_jsonTargets.cmake
fi

CMAKE_ARGS=(
    -S .
    -B build
    -G Ninja
    -U CMAKE_TOOLCHAIN_FILE
    -U nlohmann_json_DIR
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    -DCMAKE_C_COMPILER="$CLANG"
    -DCMAKE_CXX_COMPILER="$CLANGXX"
    -DDYNLEX_LLVM_VERSION="$LLVM_VERSION"
)

if [ -n "${LLVM_DIR:-}" ]; then
    CMAKE_ARGS+=("-DLLVM_DIR=$LLVM_DIR")
fi

if command -v ccache >/dev/null 2>&1; then
    CCACHE_DIR="${CCACHE_DIR:-$HOME/.cache/ccache}"
    case "$CCACHE_DIR" in
        "~/"*) CCACHE_DIR="$HOME/${CCACHE_DIR#~/}" ;;
    esac
    export CCACHE_DIR
    mkdir -p "$CCACHE_DIR"
    CMAKE_ARGS+=(
        -DCMAKE_C_COMPILER_LAUNCHER=ccache
        -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
    )
fi

cmake "${CMAKE_ARGS[@]}"

cd build

# Run clang-tidy if enabled (only on files changed since last lint)
if [ "$LINT" = "true" ]; then
    LINT_TIMESTAMP=".lint_timestamp"
    FIND_CHANGED_CPP=(find ../src -name '*.cpp')
    if [ -f "$LINT_TIMESTAMP" ]; then
        CHANGED_FILES=$("${FIND_CHANGED_CPP[@]}" -newer "$LINT_TIMESTAMP" 2>/dev/null || true)
        FILE_MSG="changed files"
    else
        CHANGED_FILES=$("${FIND_CHANGED_CPP[@]}" 2>/dev/null || true)
        FILE_MSG="all files (first run)"
    fi

    if [ -n "$CHANGED_FILES" ]; then
        echo "Checking style violations on $FILE_MSG..."
        # Only treat actual clang/clang-tidy diagnostics as errors:
        #   <file>:<line>:<column>: (warning|error|fatal error): ...
        DIAGNOSTIC_REGEX='^[^:]+:[0-9]+:[0-9]+: (warning|error|fatal error): '
        ERROR_REGEX='^[^:]+:[0-9]+:[0-9]+: (error|fatal error): '
        OUTPUT=$(echo "$CHANGED_FILES" | xargs -r "$CLANG_TIDY" -p . -quiet --header-filter='.*src/.*' 2>&1 | grep -E "$DIAGNOSTIC_REGEX" || true)
        if echo "$OUTPUT" | grep -Eq "$ERROR_REGEX"; then
            echo "$OUTPUT"
            echo "clang-tidy found errors, skipping auto-fix"
            exit 1
        else
            echo "Fixing style violations..."
            echo "$CHANGED_FILES" | xargs -r "$CLANG_TIDY" -p . -quiet -fix --header-filter='.*src/.*' 2>&1 | grep -E "$DIAGNOSTIC_REGEX" || true
            touch "$LINT_TIMESTAMP"
        fi
    fi
fi

if command -v nproc >/dev/null 2>&1; then
    JOBS="$(nproc)"
elif command -v getconf >/dev/null 2>&1; then
    JOBS="$(getconf _NPROCESSORS_ONLN)"
elif command -v sysctl >/dev/null 2>&1; then
    JOBS="$(sysctl -n hw.ncpu)"
else
    JOBS=1
fi

ninja -j"$JOBS"
