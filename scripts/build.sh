#!/bin/bash
set -e

detect_llvm_version() {
    if [ -n "${DYNLEX_LLVM_VERSION:-}" ]; then
        echo "$DYNLEX_LLVM_VERSION"
        return 0
    fi

    for version in 22 21 20; do
        if command -v "llvm-config-$version" >/dev/null 2>&1 && command -v "clang-$version" >/dev/null 2>&1; then
            echo "$version"
            return 0
        fi
    done

    if command -v llvm-config >/dev/null 2>&1 && command -v clang >/dev/null 2>&1; then
        local llvm_version
        local clang_version

        llvm_version="$(llvm-config --version | cut -d. -f1)"
        clang_version="$(clang --version | sed -n '1 s/.*clang version \([0-9][0-9]*\).*/\1/p')"

        if [ "${llvm_version:-0}" -ge 20 ] && [ "$clang_version" = "$llvm_version" ]; then
            echo "$llvm_version"
            return 0
        fi
    fi

    return 1
}

resolve_tool() {
    local base_name="$1"
    local version="$2"

    if command -v "${base_name}-${version}" >/dev/null 2>&1; then
        echo "${base_name}-${version}"
    else
        echo "${base_name}"
    fi
}

LLVM_VERSION="$(detect_llvm_version || true)"

if [ -z "$LLVM_VERSION" ]; then
    echo "Error: DynLex requires LLVM/Clang 20 or newer."
    echo "Set DYNLEX_LLVM_VERSION to an installed toolchain version or run: ./scripts/install.sh"
    exit 1
fi

CLANG="$(resolve_tool clang "$LLVM_VERSION")"
CLANGXX="$(resolve_tool clang++ "$LLVM_VERSION")"
CLANG_FORMAT="$(resolve_tool clang-format "$LLVM_VERSION")"
CLANG_TIDY="$(resolve_tool clang-tidy "$LLVM_VERSION")"

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
command -v "$CLANG_FORMAT" >/dev/null 2>&1 || MISSING_DEPS+=("$CLANG_FORMAT")
command -v "$CLANG_TIDY" >/dev/null 2>&1 || MISSING_DEPS+=("$CLANG_TIDY")
command -v cmake >/dev/null 2>&1 || MISSING_DEPS+=("cmake")
command -v ninja >/dev/null 2>&1 || MISSING_DEPS+=("ninja")

if [ ${#MISSING_DEPS[@]} -ne 0 ]; then
    echo "Error: Missing required dependencies: ${MISSING_DEPS[*]}"
    echo ""
    echo "Would you like to install missing dependencies? (y/n)"
    read -r response
    if [[ "$response" =~ ^[Yy]$ ]]; then
        ./scripts/install.sh
        echo ""
        echo "Dependencies installed. Re-running build..."
        exec "$0" "$@"
    else
        echo "Please install missing dependencies manually or run: ./scripts/install.sh"
        exit 1
    fi
fi

# Use the newest supported clang toolchain that is installed.
export CC="$CLANG"
export CXX="$CLANGXX"

# Format source files
find src -name '*.cpp' -o -name '*.hpp' -o -name '*.h' | xargs "$CLANG_FORMAT" -i

mkdir -p build
if [ -f build/CMakeCache.txt ] && rg -q "conan_toolchain\.cmake" build/CMakeCache.txt; then
    echo "Removing stale Conan-generated CMake cache..."
    rm -f build/CMakeCache.txt
    rm -rf build/CMakeFiles
fi

if [ -d build/CMakeFiles ] && rg -q "conan_toolchain\.cmake" build/CMakeFiles; then
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
        build/cmakedeps_macros.cmake \
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

cmake -S . -B build -G Ninja \
    -U CMAKE_TOOLCHAIN_FILE \
    -U nlohmann_json_DIR \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_C_COMPILER="$CLANG" \
    -DCMAKE_CXX_COMPILER="$CLANGXX" \
    -DDYNLEX_LLVM_VERSION="$LLVM_VERSION"

cd build

# Run clang-tidy if enabled (only on files changed since last lint)
if [ "$LINT" = "true" ]; then
    LINT_TIMESTAMP=".lint_timestamp"
    if [ -f "$LINT_TIMESTAMP" ]; then
        CHANGED_FILES=$(find ../src -name '*.cpp' -newer "$LINT_TIMESTAMP" 2>/dev/null || true)
        FILE_MSG="changed files"
    else
        CHANGED_FILES=$(find ../src -name '*.cpp' 2>/dev/null || true)
        FILE_MSG="all files (first run)"
    fi

    if [ -n "$CHANGED_FILES" ]; then
        echo "Checking style violations on $FILE_MSG..."
        OUTPUT=$(echo "$CHANGED_FILES" | xargs -r "$CLANG_TIDY" -p . -quiet --header-filter='.*src/.*' 2>&1 | grep -E "(warning:|error:)" || true)
        if echo "$OUTPUT" | grep -q "error:"; then
            echo "$OUTPUT"
            echo "clang-tidy found errors, skipping auto-fix"
            exit 1
        else
            echo "Fixing style violations..."
            echo "$CHANGED_FILES" | xargs -r "$CLANG_TIDY" -p . -quiet -fix --header-filter='.*src/.*' 2>&1 | grep -E "(warning:|error:)" || true
            touch "$LINT_TIMESTAMP"
        fi
    fi
fi

ninja -j$(nproc)
