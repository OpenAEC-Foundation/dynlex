#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"

FBX_PATH="${1:-$PROJECT_DIR/tests/games/model/assets/source/city.fbx}"
OBJ_PATH="${2:-$PROJECT_DIR/tests/games/model/assets/generated/city.obj}"
TRIANGLE_STEP="${3:-1}"

if [ ! -x "$PROJECT_DIR/build/dynlex" ]; then
    "$PROJECT_DIR/scripts/build.sh" --lint=false
fi

cmake --build "$PROJECT_DIR/build" --target fbx_to_obj
mkdir -p "$(dirname "$OBJ_PATH")"
"$PROJECT_DIR/build/fbx_to_obj" "$FBX_PATH" "$OBJ_PATH" --triangle-step "$TRIANGLE_STEP"
"$PROJECT_DIR/build/dynlex" "$PROJECT_DIR/tests/games/model/viewer.dl" -o "$PROJECT_DIR/tests/games/model/viewer"
"$PROJECT_DIR/tests/games/model/viewer"
