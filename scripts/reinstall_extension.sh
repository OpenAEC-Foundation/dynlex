#!/bin/bash

# Script to compile, package, and reinstall the 3BX VS Code extension
# This ensures that both the compiler (LSP server) and the extension client are up to date.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_DIR="$SCRIPT_DIR/.."

# 1. Build the C++ Compiler (LSP/DAP Server)
echo "Building 3BX compiler..."
"$SCRIPT_DIR/build.sh"

# 2. Build and Package the VS Code Extension
echo "Compiling and packaging VS Code extension..."
cd "$WORKSPACE_DIR/vscode-extension"

# Ensure dependencies are installed
if [ ! -d "node_modules" ]; then
    npm install
fi

# Compile TypeScript
npm run compile

# Package into .vsix
# We use --no-dependencies to skip the npm install during packaging as we just did it
npx vsce package --no-dependencies -o 3bx-extension.vsix

# 3. Install the extension in VS Code
echo "Reinstalling extension in VS Code..."
code --install-extension 3bx-extension.vsix --force

echo "Done! Please restart VS Code or reload the window to apply changes."
