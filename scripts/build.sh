#!/bin/bash

# Build the 3BX compiler
# Usage: ./scripts/build.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_DIR="$SCRIPT_DIR/.."
BUILD_DIR="$WORKSPACE_DIR/build"

# Third-party directories to exclude
EXCLUDE_PATTERNS="nlohmann|third_party|external|vendor"
TIMESTAMP_FILE="$BUILD_DIR/.last_check"

# Find changed C++ files (newer than last check, excluding third-party)
mkdir -p "$BUILD_DIR"
if [ -f "$TIMESTAMP_FILE" ]; then
    CHANGED_FILES=$(find "$WORKSPACE_DIR/src" "$WORKSPACE_DIR/include" \( -name "*.cpp" -o -name "*.hpp" \) -newer "$TIMESTAMP_FILE" | grep -Ev "$EXCLUDE_PATTERNS" || true)
else
    CHANGED_FILES=$(find "$WORKSPACE_DIR/src" "$WORKSPACE_DIR/include" \( -name "*.cpp" -o -name "*.hpp" \) | grep -Ev "$EXCLUDE_PATTERNS" || true)
fi

if [ -n "$CHANGED_FILES" ]; then
    # Check code conventions on changed files
    echo "$CHANGED_FILES" | xargs python3 "$SCRIPT_DIR/check_conventions.py"

    # Format changed files
    echo "$CHANGED_FILES" | xargs "$SCRIPT_DIR/format.sh"
else
    echo "No changed C++ files to check/format."
fi

# Update timestamp
touch "$TIMESTAMP_FILE"

# Create build directory if needed
mkdir -p "$BUILD_DIR"

# Configure if needed
if [ ! -f "$BUILD_DIR/Makefile" ]; then
    echo "Configuring build..."
    cd "$BUILD_DIR"
    cmake "$WORKSPACE_DIR"
    cd "$WORKSPACE_DIR"
fi

# Build
echo "Building 3bx compiler..."
cd "$BUILD_DIR"
make -j$(nproc)

echo "Build complete."
