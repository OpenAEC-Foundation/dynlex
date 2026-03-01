#!/bin/bash
set -e

detect_install_llvm_version() {
    if [ -n "${DYNLEX_LLVM_VERSION:-}" ]; then
        echo "$DYNLEX_LLVM_VERSION"
    else
        echo "20"
    fi
}

echo "Installing DynLex Compiler Dependencies..."
echo "=========================================="

# Update package list
echo "Updating package list..."
sudo apt update

# LLVM 20 is the minimum supported toolchain because DynLex relies on the
# upstream SPIR-V backend introduced in LLVM 20.
LLVM_VERSION="$(detect_install_llvm_version)"

# Install LLVM/Clang toolchain (pinned to specific version)
echo "Installing LLVM/Clang $LLVM_VERSION toolchain..."
sudo apt install -y \
    clang-$LLVM_VERSION \
    clangd-$LLVM_VERSION \
    clang-format-$LLVM_VERSION \
    clang-tidy-$LLVM_VERSION \
    llvm-$LLVM_VERSION \
    llvm-$LLVM_VERSION-dev \
    spirv-tools

# Install build tools
echo "Installing build tools..."
sudo apt install -y \
    cmake \
    ninja-build \
    git \
    python3 \
    nodejs \
    npm \
    golang-go \
    nlohmann-json3-dev

# Install MCP language server
echo "Installing MCP language server..."
go install github.com/isaacphi/mcp-language-server@latest || echo "Warning: mcp-language-server install failed (optional)"

echo ""
echo "Note: MCP server is configured in .mcp.json"
echo "Make sure to run the DynLex LSP server with: ./build/dynlex --lsp"

echo ""
echo "Installation complete!"
echo ""
echo "Installed versions:"
clang-$LLVM_VERSION --version | head -n1
clangd-$LLVM_VERSION --version | head -n1
clang-format-$LLVM_VERSION --version | head -n1
clang-tidy-$LLVM_VERSION --version | head -n1
cmake --version | head -n1
ninja --version
llvm-config-$LLVM_VERSION --version
node --version
npm --version
go version
