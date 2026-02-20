#!/bin/bash
set -e

echo "Installing DynLex Compiler Dependencies..."
echo "=========================================="

# Update package list
echo "Updating package list..."
sudo apt update

# LLVM version — LLVM 20 is required for the SPIR-V backend (official target since LLVM 20).
# When changing this, also update CMakeLists.txt and build.sh.
LLVM_VERSION=20

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

# Ensure Go bin directory is in PATH (go install puts binaries in ~/go/bin)
if ! echo "$PATH" | grep -q "$(go env GOPATH)/bin"; then
    export PATH="$(go env GOPATH)/bin:$PATH"
    SHELL_RC="$HOME/.bashrc"
    [ -f "$HOME/.zshrc" ] && SHELL_RC="$HOME/.zshrc"
    if ! grep -q 'GOPATH.*bin' "$SHELL_RC" 2>/dev/null; then
        echo 'export PATH="$(go env GOPATH)/bin:$PATH"' >> "$SHELL_RC"
        echo "Added Go bin directory to $SHELL_RC"
    fi
fi

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
conan --version
llvm-config-$LLVM_VERSION --version
node --version
npm --version
go version
