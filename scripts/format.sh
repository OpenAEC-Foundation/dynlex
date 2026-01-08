#!/bin/bash

# Format C++ files (matches VS Code's Ctrl+Shift+I with C/C++ extension)
# Usage: ./scripts/format.sh file1 [file2 ...]

if ! command -v clang-format &> /dev/null; then
    echo "Error: clang-format not found. Install with: sudo apt install clang-format"
    exit 1
fi

if [ $# -eq 0 ]; then
    echo "Usage: ./scripts/format.sh file1 [file2 ...]"
    exit 1
fi

for file in "$@"; do
    echo "Formatting: $file"
    clang-format -i "$file"
done
