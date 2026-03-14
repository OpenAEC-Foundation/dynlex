#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
COMPILER="$PROJECT_DIR/build/dynlex"
if [[ ! -x "$COMPILER" && -x "$PROJECT_DIR/build/dynlex.exe" ]]; then
	COMPILER="$PROJECT_DIR/build/dynlex.exe"
fi

if [[ $# -lt 1 ]]; then
	echo "Usage: ./scripts/compile_out.sh <file.dl> [extra dynlex args]"
	exit 1
fi

INPUT_FILE="$1"
shift

if [[ ! -f "$INPUT_FILE" ]]; then
	echo "Input file not found: $INPUT_FILE"
	exit 1
fi

"$SCRIPT_DIR/build.sh" --lint=false

for arg in "$@"; do
	if [[ "$arg" == "-o" || "$arg" == -o* ]]; then
		echo "Do not pass -o; this script sets output automatically."
		exit 1
	fi
done

OUTPUT_PATH="$INPUT_FILE"
if [[ "$OUTPUT_PATH" == *.dl ]]; then
	OUTPUT_PATH="${OUTPUT_PATH%.dl}"
fi
OUTPUT_PATH="${OUTPUT_PATH}.out"

"$COMPILER" "$INPUT_FILE" "$@" -o "$OUTPUT_PATH"
echo "$OUTPUT_PATH"
"$OUTPUT_PATH"
