#!/bin/sh
set -eu

REPO="OpenAEC-Foundation/dynlex"
API_BASE="https://api.github.com/repos/${REPO}"

need_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Error: required command '$1' is not installed." >&2
        exit 1
    fi
}

need_cmd uname
need_cmd mktemp
need_cmd curl

OS="$(uname -s)"
if [ "$OS" != "Linux" ]; then
    echo "This installer currently supports Linux only." >&2
    echo "Use https://github.com/${REPO}/releases/latest for Windows (.msi) and macOS (.pkg)." >&2
    exit 1
fi

ARCH="$(uname -m)"
case "$ARCH" in
    x86_64|amd64)
        ;;
    *)
        echo "Warning: untested architecture '$ARCH'. Attempting install anyway." >&2
        ;;
esac

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT INT TERM

if command -v dpkg >/dev/null 2>&1; then
    ASSET_PATTERN='"name": "dynlex\.deb"|"name": "dynlex-linux\.deb"|"name": "dynlex-[^"]*-Linux\.deb"'
    INSTALL_MODE="deb"
else
    ASSET_PATTERN='"name": "dynlex\.tar\.gz"|"name": "dynlex-linux\.tar\.gz"|"name": "dynlex-[^"]*-Linux\.tar\.gz"'
    INSTALL_MODE="tar"
fi

RELEASE_JSON="$(curl -fsSL "${API_BASE}/releases/latest")"
ASSET_NAME="$(printf '%s' "$RELEASE_JSON" | grep -Eo "$ASSET_PATTERN" | head -n 1 | cut -d'"' -f4 || true)"

if [ -z "$ASSET_NAME" ]; then
    echo "Error: could not find a compatible Linux asset in latest release." >&2
    exit 1
fi

ASSET_URL="https://github.com/${REPO}/releases/latest/download/${ASSET_NAME}"
ASSET_PATH="${TMP_DIR}/${ASSET_NAME}"

echo "Downloading ${ASSET_NAME}..."
curl -fL "$ASSET_URL" -o "$ASSET_PATH"

if [ "$INSTALL_MODE" = "deb" ]; then
    if [ "$(id -u)" -ne 0 ]; then
        need_cmd sudo
        echo "Installing .deb with sudo..."
        sudo dpkg -i "$ASSET_PATH" || {
            echo "Fixing dependencies..."
            sudo apt-get update
            sudo apt-get install -f -y
        }
    else
        dpkg -i "$ASSET_PATH" || {
            apt-get update
            apt-get install -f -y
        }
    fi
else
    need_cmd tar
    PREFIX="${HOME}/.local"
    mkdir -p "$PREFIX"
    tar -xzf "$ASSET_PATH" -C "$TMP_DIR"
    EXTRACTED_DIR="$(find "$TMP_DIR" -maxdepth 1 -mindepth 1 -type d -name 'dynlex-*' | head -n 1)"
    if [ -z "$EXTRACTED_DIR" ]; then
        echo "Error: failed to unpack tarball." >&2
        exit 1
    fi

    rm -rf "$PREFIX/dynlex"
    mkdir -p "$PREFIX/dynlex"
    cp -R "$EXTRACTED_DIR"/* "$PREFIX/dynlex/"

    if [ ! -x "$PREFIX/dynlex/bin/dynlex" ]; then
        echo "Error: dynlex binary not found after extraction." >&2
        exit 1
    fi

    mkdir -p "$PREFIX/bin"
    ln -sf "$PREFIX/dynlex/bin/dynlex" "$PREFIX/bin/dynlex"

    case ":$PATH:" in
        *":$PREFIX/bin:"*) ;;
        *)
            echo "Add this to your shell profile:" >&2
            echo "  export PATH=\"$PREFIX/bin:\$PATH\"" >&2
            ;;
    esac
fi

if command -v dynlex >/dev/null 2>&1; then
    echo "Installed: $(command -v dynlex)"
    dynlex --help >/dev/null 2>&1 || true
fi

echo "DynLex install complete."
