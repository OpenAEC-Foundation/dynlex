#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
EXT_DIR="$SCRIPT_DIR/../vscode-extension"
PROJECT_DIR="$SCRIPT_DIR/.."

# Build first
"$SCRIPT_DIR/build_extension.sh"

# Install the VS Code Marketplace identity into VS Code.
VERSION="$(node -p "require(process.argv[1]).version" "$EXT_DIR/package.json")"
VSIX="$PROJECT_DIR/build/vscode-extension/dynlex-language-$VERSION-vscode-marketplace.vsix"
if [ ! -f "$VSIX" ]; then
    echo "Expected VSIX was not created: $VSIX" >&2
    exit 1
fi
echo "Installing $VSIX..."
if code --list-extensions | grep -Fxiq "impertio.dynlex-language"; then
    code --uninstall-extension impertio.dynlex-language
fi
code --install-extension "$VSIX" --force

echo ""
echo "Extension installed. Please reload VS Code (Ctrl+Shift+P → 'Developer: Reload Window')."
