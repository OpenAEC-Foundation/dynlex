#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
IDE_DIR="$PROJECT_DIR/src/web/ide"
WEB_ROOT_DIR="$PROJECT_DIR/web"

if ! command -v npm >/dev/null 2>&1; then
    echo "Missing required dependency: npm"
    exit 1
fi

if [ ! -d "$IDE_DIR/node_modules" ]; then
    echo "Installing IDE dependencies..."
    (cd "$IDE_DIR" && npm ci)
fi

(cd "$IDE_DIR" && npm run build)

rm -rf "$WEB_ROOT_DIR/assets" "$WEB_ROOT_DIR/compiler" "$WEB_ROOT_DIR/ide"
mkdir -p "$WEB_ROOT_DIR/ide"
cp "$IDE_DIR/dist/index.html" "$WEB_ROOT_DIR/ide/index.html"
cp -R "$IDE_DIR/dist/assets" "$WEB_ROOT_DIR/"
cp -R "$IDE_DIR/dist/compiler" "$WEB_ROOT_DIR/"

echo "Web root refreshed at: $WEB_ROOT_DIR"
