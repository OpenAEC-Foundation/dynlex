#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MANIFEST_PATH="$SCRIPT_DIR/../web/release-manifest.txt"

usage() {
    echo "Usage: $0 name <asset-id> | names | repository" >&2
    exit 2
}

if [[ ! -f "$MANIFEST_PATH" ]]; then
    echo "Error: release manifest does not exist: $MANIFEST_PATH" >&2
    exit 1
fi

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
    ' "$MANIFEST_PATH" || {
        echo "Error: invalid release manifest: $MANIFEST_PATH" >&2
        exit 1
    }
}

validate_manifest

case "${1:-}" in
name)
    [[ $# -eq 2 ]] || usage
    asset_name="$(
        awk -v asset_id="$2" '
            $1 == "asset" && $2 == asset_id {
                print $6
                matches++
            }
            END {
                if (matches != 1) {
                    exit 1
                }
            }
        ' "$MANIFEST_PATH"
    )" || {
        echo "Error: release asset identifier does not exist: $2" >&2
        exit 1
    }
    printf '%s\n' "$asset_name"
    ;;
names)
    [[ $# -eq 1 ]] || usage
    awk '$1 == "asset" { print $6 }' "$MANIFEST_PATH"
    ;;
repository)
    [[ $# -eq 1 ]] || usage
    awk '$1 == "repository" { print $2 }' "$MANIFEST_PATH"
    ;;
*)
    usage
    ;;
esac
