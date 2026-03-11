#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
PACKAGING_DIR="$ROOT_DIR/packaging/launchpad"
TEMPLATE_DIR="$PACKAGING_DIR/debian"
EXCLUDES_FILE="$PACKAGING_DIR/source-excludes.txt"
DEFAULT_OUTPUT_DIR="$PACKAGING_DIR/out"

SERIES=""
VERSION=""
PPA_REVISION="1"
GPG_KEY=""
UPLOAD_TARGET=""
OUTPUT_DIR="$DEFAULT_OUTPUT_DIR"
MAINTAINER_NAME="${DEBFULLNAME:-$(git -C "$ROOT_DIR" config user.name || true)}"
MAINTAINER_EMAIL="${DEBEMAIL:-$(git -C "$ROOT_DIR" config user.email || true)}"

if [ -z "$MAINTAINER_NAME" ]; then
    MAINTAINER_NAME="DynLex maintainers"
fi

if [ -z "$MAINTAINER_EMAIL" ]; then
    MAINTAINER_EMAIL="maintainers@dynlex.org"
fi

usage() {
    cat <<'EOF'
Usage: build-source-package.sh --series SERIES [options]

Options:
  --series SERIES           Ubuntu series to target (noble, plucky, questing)
  --version VERSION         Upstream version or tag (defaults to exact v* tag)
  --ppa-revision N          PPA revision suffix, defaults to 1
  --gpg-key KEYID           GPG key ID used for signing
  --upload-target TARGET    Upload with dput, for example ppa:owner/dynlex
  --output-dir PATH         Directory for generated artifacts
  --maintainer-name NAME    Maintainer name for debian/changelog
  --maintainer-email EMAIL  Maintainer email for debian/changelog
  --help                    Show this message
EOF
}

normalize_version() {
    local version="$1"
    version="${version#v}"
    echo "$version" | tr '-' '~'
}

default_version() {
    local tag
    tag="$(git -C "$ROOT_DIR" describe --tags --exact-match --match 'v*' 2>/dev/null || true)"
    if [ -n "$tag" ]; then
        normalize_version "$tag"
        return 0
    fi

    echo "Unable to determine a release version from the current commit." >&2
    echo "Pass --version explicitly or run from an exact v* tag." >&2
    exit 1
}

series_to_ubuntu_version() {
    case "$1" in
        noble) echo "24.04" ;;
        plucky) echo "25.04" ;;
        questing) echo "25.10" ;;
        *)
            echo "Unsupported Ubuntu series: $1" >&2
            exit 1
            ;;
    esac
}

require_command() {
    local name="$1"
    if ! command -v "$name" >/dev/null 2>&1; then
        echo "Missing required command: $name" >&2
        exit 1
    fi
}

render_changelog() {
    local template_path="$1"
    local output_path="$2"
    local debian_version="$3"
    local series="$4"
    local upstream_version="$5"
    local maintainer_name="$6"
    local maintainer_email="$7"
    local timestamp

    timestamp="$(date -R)"

    sed \
        -e "s|@DEBIAN_VERSION@|$debian_version|g" \
        -e "s|@SERIES@|$series|g" \
        -e "s|@UPSTREAM_VERSION@|$upstream_version|g" \
        -e "s|@MAINTAINER_NAME@|$maintainer_name|g" \
        -e "s|@MAINTAINER_EMAIL@|$maintainer_email|g" \
        -e "s|@DATE_RFC2822@|$timestamp|g" \
        "$template_path" > "$output_path"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --series)
            SERIES="$2"
            shift 2
            ;;
        --version)
            VERSION="$2"
            shift 2
            ;;
        --ppa-revision)
            PPA_REVISION="$2"
            shift 2
            ;;
        --gpg-key)
            GPG_KEY="$2"
            shift 2
            ;;
        --upload-target)
            UPLOAD_TARGET="$2"
            shift 2
            ;;
        --output-dir)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        --maintainer-name)
            MAINTAINER_NAME="$2"
            shift 2
            ;;
        --maintainer-email)
            MAINTAINER_EMAIL="$2"
            shift 2
            ;;
        --help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if [ -z "$SERIES" ]; then
    usage >&2
    exit 1
fi

if [ -z "$VERSION" ]; then
    VERSION="$(default_version)"
else
    VERSION="$(normalize_version "$VERSION")"
fi

if [ -n "$UPLOAD_TARGET" ] && [ -z "$GPG_KEY" ]; then
    echo "Uploading requires --gpg-key." >&2
    exit 1
fi

require_command debuild
require_command rsync
require_command tar

if [ -n "$UPLOAD_TARGET" ]; then
    require_command dput
fi

UBUNTU_VERSION="$(series_to_ubuntu_version "$SERIES")"
DEBIAN_VERSION="${VERSION}-0ppa${PPA_REVISION}~ubuntu${UBUNTU_VERSION}.1"

mkdir -p "$OUTPUT_DIR"
SERIES_OUTPUT_DIR="$OUTPUT_DIR/$SERIES"
rm -rf "$SERIES_OUTPUT_DIR"
mkdir -p "$SERIES_OUTPUT_DIR"

WORK_DIR="$(mktemp -d "$SERIES_OUTPUT_DIR/work.XXXXXX")"
trap 'rm -rf "$WORK_DIR"' EXIT

SOURCE_DIR="$WORK_DIR/dynlex-$VERSION"
ORIG_TARBALL="$WORK_DIR/dynlex_${VERSION}.orig.tar.gz"

rsync -a --delete --exclude-from="$EXCLUDES_FILE" "$ROOT_DIR"/ "$SOURCE_DIR"/
tar -C "$WORK_DIR" -czf "$ORIG_TARBALL" "dynlex-$VERSION"

cp -R "$TEMPLATE_DIR" "$SOURCE_DIR/debian"
render_changelog \
    "$SOURCE_DIR/debian/changelog.in" \
    "$SOURCE_DIR/debian/changelog" \
    "$DEBIAN_VERSION" \
    "$SERIES" \
    "$VERSION" \
    "$MAINTAINER_NAME" \
    "$MAINTAINER_EMAIL"
rm "$SOURCE_DIR/debian/changelog.in"

pushd "$SOURCE_DIR" >/dev/null
if [ -n "$GPG_KEY" ]; then
    debuild -S -sa -k"$GPG_KEY"
else
    debuild -S -sa -us -uc
fi
popd >/dev/null

find "$WORK_DIR" -maxdepth 1 -type f \
    \( -name "*.changes" -o -name "*.buildinfo" -o -name "*.dsc" -o -name "*.debian.tar.*" -o -name "*.orig.tar.gz" \) \
    -exec mv {} "$SERIES_OUTPUT_DIR"/ \;

if [ -n "$UPLOAD_TARGET" ]; then
    dput "$UPLOAD_TARGET" "$SERIES_OUTPUT_DIR"/dynlex_"$DEBIAN_VERSION"_source.changes
fi

echo "Source package artifacts written to $SERIES_OUTPUT_DIR"
