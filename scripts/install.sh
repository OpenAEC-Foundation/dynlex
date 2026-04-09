#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/llvm_version.sh"

LLVM_VERSION="$(dynlex_install_llvm_version)"

require_sudo() {
    if ! command -v sudo >/dev/null 2>&1; then
        echo "Error: sudo is required for system package installation."
        exit 1
    fi
}

install_optional_mcp_server() {
    if command -v go >/dev/null 2>&1; then
        echo "Installing optional MCP language server..."
        go install github.com/isaacphi/mcp-language-server@latest || echo "Warning: optional MCP language server install failed"
    else
        echo "Skipping optional MCP language server install (go not found)"
    fi
}

install_linux_deps() {
    if command -v apt-get >/dev/null 2>&1; then
        require_sudo
        sudo apt-get update
        sudo apt-get install -y \
            "clang-$LLVM_VERSION" \
            "clangd-$LLVM_VERSION" \
            "clang-format-$LLVM_VERSION" \
            "clang-tidy-$LLVM_VERSION" \
            "llvm-$LLVM_VERSION" \
            "llvm-$LLVM_VERSION-dev" \
            libcurl4-openssl-dev \
            libedit-dev \
            nlohmann-json3-dev \
            ccache \
            cmake \
            ninja-build \
            git \
            python3 \
            nodejs \
            npm \
            golang-go
        return
    fi

    if command -v dnf >/dev/null 2>&1; then
        require_sudo
        sudo dnf install -y \
            clang \
            clang-tools-extra \
            llvm \
            llvm-devel \
            ccache \
            cmake \
            ninja-build \
            git \
            python3 \
            nodejs \
            npm \
            golang
        return
    fi

    if command -v pacman >/dev/null 2>&1; then
        require_sudo
        sudo pacman -Sy --noconfirm \
            clang \
            llvm \
            ccache \
            cmake \
            ninja \
            git \
            python \
            nodejs \
            npm \
            go
        return
    fi

    if command -v zypper >/dev/null 2>&1; then
        require_sudo
        sudo zypper --non-interactive refresh
        sudo zypper --non-interactive install \
            clang \
            clang-tools \
            llvm \
            llvm-devel \
            ccache \
            cmake \
            ninja \
            git \
            python3 \
            nodejs \
            npm \
            go
        return
    fi

    echo "Error: unsupported Linux package manager. Supported: apt, dnf, pacman, zypper."
    exit 1
}

install_macos_deps() {
    if ! command -v brew >/dev/null 2>&1; then
        echo "Error: Homebrew is required on macOS. Install from https://brew.sh first."
        exit 1
    fi

    brew update
    if brew info llvm@20 >/dev/null 2>&1; then
        brew install llvm@20 nlohmann-json ccache cmake ninja git node go
        BREW_LLVM_PREFIX="$(brew --prefix llvm@20)"
    else
        brew install llvm nlohmann-json ccache cmake ninja git node go
        BREW_LLVM_PREFIX="$(brew --prefix llvm)"
    fi

    echo ""
    echo "Add LLVM tools to PATH for this shell before building:"
    echo "  export PATH=\"$BREW_LLVM_PREFIX/bin:\$PATH\""
    echo ""
    echo "Or add it permanently in your shell rc file."
}

main() {
    echo "Installing DynLex build dependencies..."
    echo "LLVM minimum version: $LLVM_VERSION"

    case "$(uname -s)" in
    Linux)
        install_linux_deps
        ;;
    Darwin)
        install_macos_deps
        ;;
    *)
        echo "Error: unsupported OS for install.sh. Use scripts/install.ps1 on Windows."
        exit 1
        ;;
    esac

    install_optional_mcp_server

    echo ""
    echo "Installation complete."
    echo "Build with: ./scripts/build.sh"
}

main "$@"
