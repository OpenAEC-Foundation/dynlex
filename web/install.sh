#!/bin/sh
set -eu

BOOTSTRAP_REPOSITORY="OpenAEC-Foundation/dynlex"
FORMAT=""
PACKAGE_PATH=""
PREFIX="${HOME}/.local"

print_usage() {
    cat >&2 <<'EOF'
Usage: install.sh [--format deb|tar] [--prefix DIRECTORY] [PACKAGE]

Without PACKAGE, installs the latest Linux release for this machine. A local .deb or .tar.gz
PACKAGE can be supplied for an offline installation. --prefix applies to tar
installations and defaults to $HOME/.local.
EOF
}

usage() {
    print_usage
    exit 2
}

need_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Error: required command '$1' is not installed." >&2
        exit 1
    fi
}

manifest_field() {
    record_type="$1"
    field_number="$2"
    awk -v record_type="$record_type" -v field_number="$field_number" '
        $1 == record_type {
            print $field_number
            matches++
        }
        END {
            if (matches != 1) {
                exit 1
            }
        }
    ' "$MANIFEST_PATH"
}

manifest_asset_name() {
    requested_asset_id="$1"
    awk -v requested_asset_id="$requested_asset_id" '
        $1 == "asset" && $2 == requested_asset_id {
            if (NF != 6) {
                exit 1
            }
            print $6
            matches++
        }
        END {
            if (matches != 1) {
                exit 1
            }
        }
    ' "$MANIFEST_PATH"
}

validate_manifest() {
    awk '
        $1 == "schema" {
            if (NF != 2 || $2 != "1" || schema_count != 0) {
                exit 1
            }
            schema_count++
            next
        }
        $1 == "repository" {
            if (NF != 2 || repository_count != 0) {
                exit 1
            }
            repository_count++
            next
        }
        $1 == "asset" {
            if (NF != 6 || asset_ids[$2]++ || asset_names[$6]++) {
                exit 1
            }
            asset_count++
            next
        }
        NF != 0 && substr($1, 1, 1) != "#" {
            exit 1
        }
        END {
            if (schema_count != 1 || repository_count != 1 || asset_count == 0) {
                exit 1
            }
        }
    ' "$MANIFEST_PATH"
}

while [ "$#" -gt 0 ]; do
    case "$1" in
    --format)
        [ "$#" -ge 2 ] || usage
        FORMAT="$2"
        shift 2
        ;;
    --prefix)
        [ "$#" -ge 2 ] || usage
        PREFIX="$2"
        shift 2
        ;;
    --help|-h)
        print_usage
        exit 0
        ;;
    --*)
        usage
        ;;
    *)
        [ -z "$PACKAGE_PATH" ] || usage
        PACKAGE_PATH="$1"
        shift
        ;;
    esac
done

case "$FORMAT" in
""|deb|tar) ;;
*) usage ;;
esac

case "$PREFIX" in
/*) ;;
*)
    echo "Error: installation prefix must be an absolute path." >&2
    exit 1
    ;;
esac
if [ "$PREFIX" = "/" ]; then
    echo "Error: refusing to use the filesystem root as the installation prefix." >&2
    exit 1
fi

need_cmd uname
need_cmd mktemp
need_cmd awk

OS="$(uname -s)"
if [ "$OS" != "Linux" ]; then
    echo "This installer supports Linux only." >&2
    echo "Use https://github.com/${BOOTSTRAP_REPOSITORY}/releases/latest for Windows and macOS." >&2
    exit 1
fi

ARCH="$(uname -m)"
case "$ARCH" in
x86_64|amd64)
    ARCHITECTURE="x64"
    ;;
aarch64|arm64)
    ARCHITECTURE="arm64"
    ;;
*)
    echo "Error: unsupported Linux architecture: $ARCH" >&2
    exit 1
    ;;
esac

TEMPORARY_DIRECTORY="$(mktemp -d)"
trap 'rm -rf "$TEMPORARY_DIRECTORY"' EXIT INT TERM
MANIFEST_PATH="$TEMPORARY_DIRECTORY/release-manifest.txt"

if [ -n "$PACKAGE_PATH" ]; then
    if [ ! -f "$PACKAGE_PATH" ]; then
        echo "Error: package does not exist: $PACKAGE_PATH" >&2
        exit 1
    fi
    case "$PACKAGE_PATH" in
    *.deb) PACKAGE_FORMAT="deb" ;;
    *.tar.gz) PACKAGE_FORMAT="tar" ;;
    *)
        echo "Error: local package must end in .deb or .tar.gz." >&2
        exit 1
        ;;
    esac
    if [ -n "$FORMAT" ] && [ "$FORMAT" != "$PACKAGE_FORMAT" ]; then
        echo "Error: --format does not match the local package." >&2
        exit 1
    fi
    FORMAT="$PACKAGE_FORMAT"
    ASSET_PATH="$PACKAGE_PATH"
else
    need_cmd curl
    need_cmd sha256sum

    MANIFEST_URL="https://github.com/${BOOTSTRAP_REPOSITORY}/releases/latest/download/release-manifest.txt"
    curl -fL "$MANIFEST_URL" -o "$MANIFEST_PATH"
    if ! validate_manifest; then
        echo "Error: invalid release manifest." >&2
        exit 1
    fi
    REPOSITORY="$(manifest_field repository 2)" || {
        echo "Error: invalid release manifest." >&2
        exit 1
    }
    if [ "$REPOSITORY" != "$BOOTSTRAP_REPOSITORY" ]; then
        echo "Error: release manifest names an unexpected repository." >&2
        exit 1
    fi

    if [ -z "$FORMAT" ]; then
        if command -v dpkg >/dev/null 2>&1; then
            FORMAT="deb"
        else
            FORMAT="tar"
        fi
    fi

    if [ "$FORMAT" = "deb" ]; then
        ASSET_ID="linux-${ARCHITECTURE}-deb"
    else
        ASSET_ID="linux-${ARCHITECTURE}-tar"
    fi
    ASSET_NAME="$(manifest_asset_name "$ASSET_ID")" || {
        echo "Error: release manifest does not contain the required Linux package." >&2
        exit 1
    }
    ASSET_PATH="$TEMPORARY_DIRECTORY/$ASSET_NAME"
    ASSET_URL="https://github.com/${REPOSITORY}/releases/latest/download/${ASSET_NAME}"
    CHECKSUMS_PATH="$TEMPORARY_DIRECTORY/SHA256SUMS"
    CHECKSUMS_URL="https://github.com/${REPOSITORY}/releases/latest/download/SHA256SUMS"

    echo "Downloading ${ASSET_NAME}..."
    curl -fL "$ASSET_URL" -o "$ASSET_PATH"
    curl -fL "$CHECKSUMS_URL" -o "$CHECKSUMS_PATH"

    EXPECTED_CHECKSUM="$(
        awk -v asset_name="$ASSET_NAME" '
            $2 == asset_name && $1 ~ /^[0-9a-fA-F]{64}$/ {
                print tolower($1)
                matches++
            }
            END {
                if (matches != 1) {
                    exit 1
                }
            }
        ' "$CHECKSUMS_PATH"
    )" || {
        echo "Error: release checksums do not contain the selected package." >&2
        exit 1
    }
    ACTUAL_CHECKSUM="$(sha256sum "$ASSET_PATH" | awk '{ print tolower($1) }')"
    if [ "$ACTUAL_CHECKSUM" != "$EXPECTED_CHECKSUM" ]; then
        echo "Error: downloaded package checksum does not match the release." >&2
        exit 1
    fi
fi

if [ "$FORMAT" = "deb" ]; then
    need_cmd dpkg
    if [ "$(id -u)" -ne 0 ]; then
        need_cmd sudo
        echo "Installing .deb with sudo..."
        if ! sudo dpkg -i "$ASSET_PATH"; then
            need_cmd apt-get
            echo "Installing missing package dependencies..."
            sudo apt-get update
            sudo apt-get install -f -y
        fi
    elif ! dpkg -i "$ASSET_PATH"; then
        need_cmd apt-get
        apt-get update
        apt-get install -f -y
    fi

    if ! command -v dynlex >/dev/null 2>&1; then
        echo "Error: dynlex was not found after installing the Debian package." >&2
        exit 1
    fi
    INSTALLED_BINARY="$(command -v dynlex)"
else
    need_cmd tar
    need_cmd cp
    need_cmd ln
    need_cmd mkdir

    ARCHIVE_ROOT="$(
        tar -tzf "$ASSET_PATH" | awk -F/ '
            {
                if ($1 == "" || $1 == "." || $1 == "..") {
                    exit 1
                }
                for (field_index = 1; field_index <= NF; field_index++) {
                    if ($field_index == "..") {
                        exit 1
                    }
                }
                if (archive_root == "") {
                    archive_root = $1
                } else if ($1 != archive_root) {
                    exit 1
                }
                entries++
            }
            END {
                if (entries == 0 || archive_root == "") {
                    exit 1
                }
                print archive_root
            }
        '
    )" || {
        echo "Error: release archive does not have one safe top-level directory." >&2
        exit 1
    }

    tar -xzf "$ASSET_PATH" -C "$TEMPORARY_DIRECTORY"
    EXTRACTED_DIRECTORY="$TEMPORARY_DIRECTORY/$ARCHIVE_ROOT"
    if [ ! -d "$EXTRACTED_DIRECTORY" ]; then
        echo "Error: release archive did not extract to its declared top-level directory." >&2
        exit 1
    fi

    rm -rf "$PREFIX/dynlex"
    mkdir -p "$PREFIX/dynlex"
    cp -R "$EXTRACTED_DIRECTORY"/. "$PREFIX/dynlex/"

    INSTALLED_BINARY="$PREFIX/dynlex/bin/dynlex"
    if [ ! -x "$INSTALLED_BINARY" ]; then
        echo "Error: dynlex binary was not installed from the release archive." >&2
        exit 1
    fi

    mkdir -p "$PREFIX/bin"
    ln -sf "$INSTALLED_BINARY" "$PREFIX/bin/dynlex"

    case ":$PATH:" in
    *":$PREFIX/bin:"*) ;;
    *)
        echo "Add this to your shell profile:" >&2
        echo "  export PATH=\"$PREFIX/bin:\$PATH\"" >&2
        ;;
    esac
fi

"$INSTALLED_BINARY" --help >/dev/null
echo "Installed: $INSTALLED_BINARY"
echo "DynLex install complete."
