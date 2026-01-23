#!/bin/bash
# Setup script for lsp-mcp

set -e

echo "Setting up lsp-mcp..."

# Install npm dependencies
echo "Installing npm packages..."
npm install

# Check for clangd
if command -v clangd &> /dev/null; then
    echo "✓ clangd found: $(which clangd)"
else
    echo "✗ clangd not found. Install with:"
    echo "  Ubuntu/Debian: sudo apt install clangd"
    echo "  macOS: brew install llvm"
    exit 1
fi

# Get absolute path
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Create config file
CONFIG_FILE="$HOME/.config/claude-code/config.json"
echo ""
echo "To enable in Claude Code, add this to $CONFIG_FILE:"
echo ""
cat <<EOF
{
  "mcpServers": {
    "lsp": {
      "command": "node",
      "args": ["$SCRIPT_DIR/index.js"],
      "env": {
        "PROJECT_ROOT": "$PROJECT_ROOT"
      }
    }
  }
}
EOF

echo ""
echo "Setup complete!"
echo ""
echo "To use with 3BX files, make sure to run:"
echo "  ./build/3bx --lsp"
