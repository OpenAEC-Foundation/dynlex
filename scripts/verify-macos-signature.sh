#!/bin/bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "Usage: $0 <notarized-package> <installed-binary>" >&2
    exit 2
fi

for command in codesign pkgutil spctl xcrun; do
    if ! command -v "$command" >/dev/null 2>&1; then
        echo "Required macOS signing tool is unavailable: $command" >&2
        exit 1
    fi
done

: "${APPLE_APPLICATION_SIGNING_IDENTITY:?APPLE_APPLICATION_SIGNING_IDENTITY is required}"
: "${APPLE_INSTALLER_SIGNING_IDENTITY:?APPLE_INSTALLER_SIGNING_IDENTITY is required}"
: "${APPLE_TEAM_ID:?APPLE_TEAM_ID is required}"

package_path="$1"
binary_path="$2"

if [[ ! -f "$package_path" ]]; then
    echo "macOS installer package does not exist: $package_path" >&2
    exit 1
fi
if [[ ! -f "$binary_path" || ! -x "$binary_path" ]]; then
    echo "Installed DynLex executable does not exist or is not executable: $binary_path" >&2
    exit 1
fi

package_signature="$(pkgutil --check-signature "$package_path" 2>&1)"
printf '%s\n' "$package_signature"
if ! grep -Fq "$APPLE_INSTALLER_SIGNING_IDENTITY" <<<"$package_signature"; then
    echo "The installer is not signed by the expected Apple Developer ID Installer identity." >&2
    exit 1
fi

xcrun stapler validate "$package_path"
spctl --assess --type install --verbose=4 "$package_path"

codesign --verify --strict --verbose=2 "$binary_path"
binary_signature="$(codesign --display --verbose=4 "$binary_path" 2>&1)"
printf '%s\n' "$binary_signature"
if ! grep -Fq "Authority=$APPLE_APPLICATION_SIGNING_IDENTITY" <<<"$binary_signature"; then
    echo "The DynLex executable is not signed by the expected Apple Developer ID Application identity." >&2
    exit 1
fi
if ! grep -Fq "TeamIdentifier=$APPLE_TEAM_ID" <<<"$binary_signature"; then
    echo "The DynLex executable has an unexpected Apple Team ID." >&2
    exit 1
fi
if ! grep -Eq '^CodeDirectory .*flags=.*\(runtime\)' <<<"$binary_signature"; then
    echo "The DynLex executable does not have the hardened runtime enabled." >&2
    exit 1
fi
if ! grep -Eq '^Timestamp=' <<<"$binary_signature"; then
    echo "The DynLex executable does not have a trusted signing timestamp." >&2
    exit 1
fi
