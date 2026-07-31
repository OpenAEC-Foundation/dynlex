#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
MANIFEST_PATH="$PROJECT_DIR/metadata/release-manifest.txt"

if [[ $# -ne 2 ]]; then
    echo "Usage: $0 <artifact-directory> <output-directory>" >&2
    exit 2
fi

artifact_directory="$1"
output_directory="$2"

if [[ ! -d "$artifact_directory" ]]; then
    echo "Error: artifact directory does not exist: $artifact_directory" >&2
    exit 1
fi
if [[ -e "$output_directory" ]]; then
    echo "Error: output path already exists: $output_directory" >&2
    exit 1
fi

for command_name in cp find mkdir sha256sum sort; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Error: required command is unavailable: $command_name" >&2
        exit 1
    fi
done

mapfile -t expected_names < <("$SCRIPT_DIR/release-asset.sh" names)
declare -A expected_by_name=()
declare -A source_path_by_name=()
for expected_name in "${expected_names[@]}"; do
    expected_by_name["$expected_name"]=1
done

while IFS= read -r artifact_path; do
    artifact_name="$(basename "$artifact_path")"
    if [[ -z "${expected_by_name[$artifact_name]:-}" ]]; then
        echo "Error: unexpected release artifact: $artifact_path" >&2
        exit 1
    fi
done < <(
    find "$artifact_directory" -type f \
        \( -name '*.deb' -o -name '*.tar.gz' -o -name '*.msi' -o -name '*.pkg' \) \
        -print | sort
)

for expected_name in "${expected_names[@]}"; do
    mapfile -t matches < <(find "$artifact_directory" -type f -name "$expected_name" -print)
    if [[ ${#matches[@]} -ne 1 ]]; then
        echo "Error: expected exactly one $expected_name artifact, found ${#matches[@]}." >&2
        exit 1
    fi
    source_path_by_name["$expected_name"]="${matches[0]}"
done

mkdir "$output_directory"
for expected_name in "${expected_names[@]}"; do
    cp "${source_path_by_name[$expected_name]}" "$output_directory/$expected_name"
done
cp "$MANIFEST_PATH" "$output_directory/release-manifest.txt"

(
    cd "$output_directory"
    sha256sum "${expected_names[@]}" release-manifest.txt > SHA256SUMS
    sha256sum --check SHA256SUMS
)
