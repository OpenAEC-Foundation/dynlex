#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/llvm_toolchain.sh"

if [ "$#" -ne 1 ] || { [ "$1" != native ] && [ "$1" != web ]; }; then
	echo "Usage: $0 <native|web>" >&2
	exit 2
fi

dynlex_ensure_llvm_toolchain "$1"
