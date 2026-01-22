#!/bin/bash
set -e

# Parse arguments
LINT=true
for arg in "$@"; do
    case $arg in
        --lint=false) LINT=false ;;
        --lint=true) LINT=true ;;
    esac
done

# Use clang compiler
export CC=clang
export CXX=clang++

# Format source files
find src -name '*.cpp' -o -name '*.hpp' -o -name '*.h' | xargs clang-format -i

conan install . --output-folder=build --build=missing --settings=build_type=Debug --settings=compiler=clang --settings=compiler.version=18

mkdir -p build
cd build

cmake .. -G Ninja -DCMAKE_TOOLCHAIN_FILE=./conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++

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
        OUTPUT=$(echo "$CHANGED_FILES" | xargs -r clang-tidy -p . -quiet --header-filter='.*src/.*' 2>&1 | grep -E "(warning:|error:)" || true)
        if echo "$OUTPUT" | grep -q "error:"; then
            echo "$OUTPUT"
            echo "clang-tidy found errors, skipping auto-fix"
            exit 1
        else
            echo "Fixing style violations..."
            echo "$CHANGED_FILES" | xargs -r clang-tidy -p . -quiet -fix --header-filter='.*src/.*' 2>&1 | grep -E "(warning:|error:)" || true
            touch "$LINT_TIMESTAMP"
        fi
    fi
fi

ninja -j$(nproc)