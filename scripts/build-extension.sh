#!/bin/bash
set -e

# Load nvm if available (VS Code tasks don't inherit interactive shell)
export NVM_DIR="$HOME/.nvm"
[ -s "$NVM_DIR/nvm.sh" ] && . "$NVM_DIR/nvm.sh"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
EXT_DIR="$SCRIPT_DIR/../vscode-extension"

cd "$EXT_DIR"

# Install dependencies if needed
if [ ! -d "node_modules" ]; then
    echo "Installing dependencies..."
    npm install
fi

# Copy LICENSE from project root
cp "$SCRIPT_DIR/../LICENSE.md" .

# Bundle
echo "Bundling extension..."
npm run bundle

# Package .vsix
echo "Packaging .vsix..."
npx @vscode/vsce package --allow-missing-repository

# Install into VS Code
VSIX=$(ls -t dynlex-language-*.vsix | head -1)
echo "Installing $VSIX..."
code --install-extension "$VSIX"

echo ""
echo "Extension installed. Please reload VS Code (Ctrl+Shift+P → 'Developer: Reload Window')."
