#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
EXT_DIR="$SCRIPT_DIR/../vscode-extension"

# Build first
"$SCRIPT_DIR/build_extension.sh"

# Install into VS Code
cd "$EXT_DIR"
VSIX=$(ls -t dynlex-language-*.vsix | head -1)
echo "Installing $VSIX..."
if code --list-extensions | grep -Fxiq "impertio.dynlex-language"; then
    code --uninstall-extension impertio.dynlex-language
fi
code --install-extension "$VSIX" --force

echo ""
echo "Extension installed. Please reload VS Code (Ctrl+Shift+P → 'Developer: Reload Window')."
