#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
for dependency in node python3; do
    if ! command -v "$dependency" >/dev/null 2>&1; then
        echo "Missing required dependency: $dependency"
        exit 1
    fi
done

exec python3 "$SCRIPT_DIR/build_site.py" "$@"
