#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
COMPILER="${DYNLEX_DEBUG_COMPILER:-$PROJECT_DIR/build/dynlex}"
if [[ ! -x "$COMPILER" && -x "$PROJECT_DIR/build/dynlex.exe" ]]; then
    COMPILER="$PROJECT_DIR/build/dynlex.exe"
fi

if ! command -v gdb >/dev/null 2>&1; then
    echo "gdb not found"
    exit 1
fi

if [[ ! -x "$COMPILER" ]]; then
    echo "build/dynlex not found. Run ./scripts/build.sh first."
    exit 1
fi

gdb_args=()
dynlex_args=()
if [[ "${1:-}" == "--gdb" ]]; then
    shift
    while [[ $# -gt 0 ]]; do
        if [[ "$1" == "--" ]]; then
            shift
            break
        fi
        gdb_args+=("$1")
        shift
    done
fi
dynlex_args=("$@")

export DYNLEX_GDB_SCRIPT_DIR="$PROJECT_DIR/scripts/gdb"
for arg in "${gdb_args[@]}"; do
    if [[ "$arg" == "-p" || "$arg" == "--pid" ]]; then
        exec gdb "${gdb_args[@]}" -iex "source $PROJECT_DIR/scripts/gdb/dynlex_pretty_printers.gdb"
    fi
done
exec gdb "${gdb_args[@]}" -iex "source $PROJECT_DIR/scripts/gdb/dynlex_pretty_printers.gdb" --args "$COMPILER" "${dynlex_args[@]}"
