#!/bin/bash
set -e

echo "Installing 3BX Compiler Dependencies..."
echo "========================================"

# Update package list
echo "Updating package list..."
sudo apt update

# Install LLVM/Clang toolchain
echo "Installing LLVM/Clang toolchain..."
sudo apt install -y \
    clang \
    clangd \
    clang-format \
    clang-tidy \
    llvm \
    llvm-dev

# Install build tools
echo "Installing build tools..."
sudo apt install -y \
    cmake \
    ninja-build \
    git \
    python3 \
    pipx \
    nodejs \
    npm

# Ensure pipx path
pipx ensurepath

# Install Conan via pipx
echo "Installing Conan package manager..."
pipx install conan

# Initialize Conan profile if not exists
if [ ! -f "$HOME/.conan2/profiles/default" ]; then
    echo "Initializing Conan profile..."
    conan profile detect --force
fi

# Set up lsp_mcp
echo "Setting up lsp_mcp..."
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$PROJECT_ROOT/lsp_mcp"
npm install

# Create Claude Code config directory if needed
mkdir -p "$HOME/.config/claude-code"

# Add MCP server config (append or create)
CONFIG_FILE="$HOME/.config/claude-code/config.json"
if [ ! -f "$CONFIG_FILE" ]; then
    echo "{}" > "$CONFIG_FILE"
fi

# Use jq if available, otherwise manual JSON
if command -v jq &> /dev/null; then
    TMP_FILE=$(mktemp)
    jq --arg path "$PROJECT_ROOT/lsp_mcp/index.js" \
       --arg root "$PROJECT_ROOT" \
       '.mcpServers.lsp = {
          command: "node",
          args: [$path],
          env: { PROJECT_ROOT: $root }
       }' "$CONFIG_FILE" > "$TMP_FILE"
    mv "$TMP_FILE" "$CONFIG_FILE"
else
    echo "Note: jq not found. Add this to $CONFIG_FILE manually:"
    echo ""
    cat <<EOF
{
  "mcpServers": {
    "lsp": {
      "command": "node",
      "args": ["$PROJECT_ROOT/lsp_mcp/index.js"],
      "env": {
        "PROJECT_ROOT": "$PROJECT_ROOT"
      }
    }
  }
}
EOF
    echo ""
fi

cd "$PROJECT_ROOT"

echo ""
echo "✓ Installation complete!"
echo ""
echo "Installed versions:"
clang --version | head -n1
clangd --version | head -n1
clang-format --version | head -n1
clang-tidy --version | head -n1
cmake --version | head -n1
ninja --version
conan --version
llvm-config --version
node --version
npm --version
echo ""
echo "✓ lsp_mcp configured"
