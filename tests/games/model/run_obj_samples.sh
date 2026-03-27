#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"

python3 "$SCRIPT_DIR/prepare_obj_samples.py" >/tmp/dynlex_prepare_obj_samples.log

case "${1:-}" in
    1|commercial)
        SAMPLE_OBJ="$SCRIPT_DIR/assets/samples/choices/commercial_tower_j/building-j.obj"
        SAMPLE_LABEL="Commercial Tower J"
        ;;
    2|suburban)
        SAMPLE_OBJ="$SCRIPT_DIR/assets/samples/choices/suburban_house_t/building-type-t.obj"
        SAMPLE_LABEL="Suburban House T"
        ;;
    3|industrial)
        SAMPLE_OBJ="$SCRIPT_DIR/assets/samples/choices/industrial_factory_b/building-b.obj"
        SAMPLE_LABEL="Industrial Factory B"
        ;;
    *)
        cat <<'EOF'
Usage: tests/games/model/run_obj_samples.sh <choice>

Choices:
  1 | commercial  - Commercial Tower J
  2 | suburban    - Suburban House T
  3 | industrial  - Industrial Factory B
EOF
        exit 1
        ;;
esac

if [ ! -x "$PROJECT_DIR/build/dynlex" ]; then
    "$PROJECT_DIR/scripts/build.sh" --lint=false
fi

echo "Launching sample: $SAMPLE_LABEL"
echo "OBJ: $SAMPLE_OBJ"

"$PROJECT_DIR/build/dynlex" "$PROJECT_DIR/tests/games/model/viewer.dl" -o "$PROJECT_DIR/tests/games/model/viewer"
DYNLEX_MODEL_PATH="$SAMPLE_OBJ" "$PROJECT_DIR/tests/games/model/viewer"
