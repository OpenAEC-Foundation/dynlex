#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/llvm_toolchain.sh"

CLANG_VERSION="$DYNLEX_LLVM_BOOTSTRAP_CLANG_VERSION"

if [ "$#" -ne 0 ]; then
    echo "Usage: $0" >&2
    exit 2
fi

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
            "clang-$CLANG_VERSION" \
            "clangd-$CLANG_VERSION" \
            "clang-format-$CLANG_VERSION" \
            "clang-tidy-$CLANG_VERSION" \
            binutils \
            libfreetype-dev \
            libgl-dev \
            libglfw3-dev \
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
            binutils \
            freetype-devel \
            glfw-devel \
            json-devel \
            mesa-libGL-devel \
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
        echo "Arch Linux requires a full system upgrade before installing DynLex build dependencies."
        sudo pacman -Syu --needed --noconfirm \
            clang \
            freetype2 \
            glfw \
            libglvnd \
            binutils \
            nlohmann-json \
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
            binutils \
            freetype2-devel \
            libglfw-devel \
            Mesa-libGL-devel \
            nlohmann_json-devel \
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
    if brew info "llvm@$CLANG_VERSION" >/dev/null 2>&1; then
        BREW_LLVM_FORMULA="llvm@$CLANG_VERSION"
    else
        BREW_LLVM_FORMULA="llvm"
    fi

    local missing_formulas=()
    local formula
    for formula in "$BREW_LLVM_FORMULA" nlohmann-json freetype glfw; do
        if ! brew list --formula --versions "$formula" >/dev/null 2>&1; then
            missing_formulas+=("$formula")
        fi
    done

    local tool_formulas=(
        ccache ccache
        cmake cmake
        ninja ninja
        git git
        node node
        go go
    )
    local index
    local tool
    for ((index = 0; index < ${#tool_formulas[@]}; index += 2)); do
        tool="${tool_formulas[index]}"
        formula="${tool_formulas[index + 1]}"
        if ! command -v "$tool" >/dev/null 2>&1; then
            missing_formulas+=("$formula")
        fi
    done

    if [ ${#missing_formulas[@]} -gt 0 ]; then
        HOMEBREW_NO_INSTALL_UPGRADE=1 brew install "${missing_formulas[@]}"
    fi
    BREW_LLVM_PREFIX="$(brew --prefix "$BREW_LLVM_FORMULA")"

    BREW_LIBRARY_PATH="$(brew --prefix glfw)/lib:$(brew --prefix freetype)/lib:$(brew --prefix)/lib${LIBRARY_PATH:+:$LIBRARY_PATH}"
    export LIBRARY_PATH="$BREW_LIBRARY_PATH"
    if [ -n "${GITHUB_PATH:-}" ]; then
        printf '%s\n' "$BREW_LLVM_PREFIX/bin" >> "$GITHUB_PATH"
    fi
    if [ -n "${GITHUB_ENV:-}" ]; then
        printf 'LIBRARY_PATH=%s\n' "$BREW_LIBRARY_PATH" >> "$GITHUB_ENV"
    fi

    echo ""
    echo "Add LLVM tools to PATH for this shell before building:"
    echo "  export PATH=\"$BREW_LLVM_PREFIX/bin:\$PATH\""
    echo "  export LIBRARY_PATH=\"$BREW_LIBRARY_PATH\""
    echo ""
    echo "Or add it permanently in your shell rc file."
}

main() {
    echo "Installing DynLex build dependencies..."
    echo "Bootstrap Clang version: $CLANG_VERSION"

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
