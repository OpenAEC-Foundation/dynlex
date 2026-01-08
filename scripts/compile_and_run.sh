#!/bin/bash

# Compile and run a .3bx file
# Usage: ./scripts/compile_and_run.sh path/to/file.3bx

set -e

TIMEOUT=20

if [ -z "$1" ]; then
    echo "Usage: $0 <file.3bx>"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_DIR="$SCRIPT_DIR/.."
BUILD_DIR="$WORKSPACE_DIR/build"
SOURCE_FILE="$1"
IR_FILE="/tmp/3bx_output.ll"

# Build the compiler
"$SCRIPT_DIR/build.sh"

# Check if source file exists
if [ ! -f "$SOURCE_FILE" ]; then
    echo "Error: Source file not found: $SOURCE_FILE"
    exit 1
fi

# Compile to LLVM IR (with timeout)
echo "Compiling: $SOURCE_FILE"
timeout $TIMEOUT "$BUILD_DIR/3bx" --emit-ir "$SOURCE_FILE" > "$IR_FILE"

# Run the IR using lli (LLVM interpreter) (with timeout)
echo "Running..."
timeout $TIMEOUT lli "$IR_FILE"
