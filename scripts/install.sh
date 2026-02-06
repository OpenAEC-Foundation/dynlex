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
    npm \
    golang-go

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

# Install MCP language server
echo "Installing MCP language server..."
go install github.com/isaacphi/mcp-language-server@latest

echo ""
echo "Note: MCP server is configured in .mcp.json"
echo "Make sure to run the 3BX LSP server with: ./build/3bx --lsp"

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
go version
mcp-language-server --version 2>&1 || echo "mcp-language-server installed (no version flag)"
