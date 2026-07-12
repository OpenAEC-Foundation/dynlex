#!/bin/bash
set -e

# Load nvm if available (VS Code tasks don't inherit interactive shell)
export NVM_DIR="$HOME/.nvm"
[ -s "$NVM_DIR/nvm.sh" ] && . "$NVM_DIR/nvm.sh"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
EXT_DIR="$SCRIPT_DIR/../vscode-extension"

cd "$EXT_DIR"

# Install the exact locked dependency graph.
echo "Installing dependencies..."
npm ci

# Validate source and managed-server launch behavior before packaging.
echo "Testing extension..."
npm test
npm run lint

# Bundle
echo "Bundling extension..."
npm run bundle

# Package .vsix
echo "Packaging .vsix..."
npm run package
