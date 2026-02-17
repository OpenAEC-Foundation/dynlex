#!/bin/bash
set -e

# LLVM version — must match install.sh and CMakeLists.txt
LLVM_VERSION=20

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

command -v clang-$LLVM_VERSION >/dev/null 2>&1 || MISSING_DEPS+=("clang-$LLVM_VERSION")
command -v clang-format-$LLVM_VERSION >/dev/null 2>&1 || MISSING_DEPS+=("clang-format-$LLVM_VERSION")
command -v clang-tidy-$LLVM_VERSION >/dev/null 2>&1 || MISSING_DEPS+=("clang-tidy-$LLVM_VERSION")
command -v cmake >/dev/null 2>&1 || MISSING_DEPS+=("cmake")
command -v ninja >/dev/null 2>&1 || MISSING_DEPS+=("ninja")
command -v conan >/dev/null 2>&1 || MISSING_DEPS+=("conan")

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

# Use versioned clang compiler
export CC=clang-$LLVM_VERSION
export CXX=clang++-$LLVM_VERSION

# Format source files
find src -name '*.cpp' -o -name '*.hpp' -o -name '*.h' | xargs clang-format-$LLVM_VERSION -i

conan install . --output-folder=build --build=missing --settings=build_type=$BUILD_TYPE --settings=compiler=clang --settings=compiler.version=$LLVM_VERSION

mkdir -p build
cd build

cmake .. -G Ninja -DCMAKE_TOOLCHAIN_FILE=./conan_toolchain.cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE -DCMAKE_C_COMPILER=clang-$LLVM_VERSION -DCMAKE_CXX_COMPILER=clang++-$LLVM_VERSION

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
        OUTPUT=$(echo "$CHANGED_FILES" | xargs -r clang-tidy-$LLVM_VERSION -p . -quiet --header-filter='.*src/.*' 2>&1 | grep -E "(warning:|error:)" || true)
        if echo "$OUTPUT" | grep -q "error:"; then
            echo "$OUTPUT"
            echo "clang-tidy found errors, skipping auto-fix"
            exit 1
        else
            echo "Fixing style violations..."
            echo "$CHANGED_FILES" | xargs -r clang-tidy-$LLVM_VERSION -p . -quiet -fix --header-filter='.*src/.*' 2>&1 | grep -E "(warning:|error:)" || true
            touch "$LINT_TIMESTAMP"
        fi
    fi
fi

ninja -j$(nproc)
