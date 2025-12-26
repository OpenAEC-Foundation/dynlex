#!/bin/bash

# Compile and run a .3bx file
# Usage: ./compile_and_run.sh path/to/file.3bx

set -e

if [ -z "$1" ]; then
    echo "Usage: $0 <file.3bx>"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
SOURCE_FILE="$1"
IR_FILE="/tmp/3bx_output.ll"

# Build the compiler if needed
if [ ! -f "$BUILD_DIR/3bx" ]; then
    echo "Building 3bx compiler..."
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    cmake ..
    make -j$(nproc)
    cd "$SCRIPT_DIR"
fi

# Check if source file exists
if [ ! -f "$SOURCE_FILE" ]; then
    echo "Error: Source file not found: $SOURCE_FILE"
    exit 1
fi

# Compile to LLVM IR
echo "Compiling: $SOURCE_FILE"
"$BUILD_DIR/3bx" --emit-ir "$SOURCE_FILE" > "$IR_FILE"

# Run the IR using lli (LLVM interpreter)
echo "Running..."
lli "$IR_FILE"
