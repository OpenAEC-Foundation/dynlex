#!/bin/bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 <installation-root>" >&2
    exit 2
fi

installation_root="$1"
case "$installation_root" in
/*) ;;
*)
    echo "Error: installation root must be absolute." >&2
    exit 1
    ;;
esac

for command in brew otool install_name_tool shasum; do
    if ! command -v "$command" >/dev/null 2>&1; then
        echo "Error: required macOS dependency staging command is missing: $command" >&2
        exit 1
    fi
done

destination="$installation_root/usr/local/lib/dynlex/dependencies"
if [[ -e "$destination" ]]; then
    echo "Error: macOS dependency destination already exists: $destination" >&2
    exit 1
fi
mkdir -p "$destination/licenses"

bookkeeping="$(mktemp -d "${TMPDIR:-/tmp}/dynlex-macos-stage.XXXXXX")"
trap 'rm -rf "$bookkeeping"' EXIT
mapping_file="$bookkeeping/libraries.tsv"
licensed_formulae="$bookkeeping/licensed-formulae.txt"
manifest_entries="$bookkeeping/dependency-manifest.txt"
: > "$mapping_file"
: > "$licensed_formulae"
: > "$manifest_entries"

validate_field() {
    local value="$1"
    local description="$2"
    case "$value" in
    *$'\t'*|*$'\n'*)
        echo "Error: $description contains a tab or newline: $value" >&2
        exit 1
        ;;
    esac
}

mapping_destination() {
    local source_path="$1"
    awk -F '\t' -v source_path="$source_path" '
        $1 == source_path {
            if (found) {
                exit 2
            }
            print $2
            found = 1
        }
        END {
            if (!found) {
                exit 1
            }
        }
    ' "$mapping_file"
}

add_mapping() {
    local source_path="$1"
    local destination_name="$2"
    local existing_source

    validate_field "$source_path" "macOS dependency path"
    validate_field "$destination_name" "macOS dependency destination name"
    if mapping_destination "$source_path" >/dev/null 2>&1; then
        echo "Error: duplicate macOS dependency source: $source_path" >&2
        exit 1
    fi
    existing_source="$(
        awk -F '\t' -v destination_name="$destination_name" '
            $2 == destination_name {
                print $1
                exit
            }
        ' "$mapping_file"
    )"
    if [[ -n "$existing_source" ]]; then
        echo "Error: macOS dependency basename collision: $destination_name" >&2
        exit 1
    fi
    printf '%s\t%s\n' "$source_path" "$destination_name" >> "$mapping_file"
}

glfw_source="$(brew --prefix glfw)/lib/libglfw.3.dylib"
freetype_source="$(brew --prefix freetype)/lib/libfreetype.6.dylib"
for source_path in "$glfw_source" "$freetype_source"; do
    if [[ ! -f "$source_path" ]]; then
        echo "Error: required Homebrew library is missing: $source_path" >&2
        exit 1
    fi
done
add_mapping "$glfw_source" "libglfw.dylib"
add_mapping "$freetype_source" "libfreetype.dylib"

index=1
while :; do
    mapping_line="$(sed -n "${index}p" "$mapping_file")"
    if [[ -z "$mapping_line" ]]; then
        break
    fi
    source_path="${mapping_line%%$'\t'*}"
    while IFS= read -r dependency; do
        case "$dependency" in
        /System/Library/*|/usr/lib/*) continue ;;
        /*) ;;
        *)
            echo "Error: unsupported non-absolute macOS library dependency: $dependency" >&2
            exit 1
            ;;
        esac
        if [[ ! -f "$dependency" ]]; then
            echo "Error: macOS library dependency is missing: $dependency" >&2
            exit 1
        fi
        if ! mapping_destination "$dependency" >/dev/null 2>&1; then
            add_mapping "$dependency" "$(basename "$dependency")"
        fi
    done < <(otool -L "$source_path" | tail -n +2 | sed -E 's/^[[:space:]]*([^[:space:]]+).*/\1/')
    index=$((index + 1))
done

while IFS=$'\t' read -r source_path destination_name; do
    cp "$source_path" "$destination/$destination_name"
    chmod u+w "$destination/$destination_name"
done < "$mapping_file"

while IFS=$'\t' read -r source_path destination_name; do
    destination_path="$destination/$destination_name"
    install_name_tool -id "@rpath/$destination_name" "$destination_path"
    while IFS= read -r dependency; do
        if replacement_name="$(mapping_destination "$dependency" 2>/dev/null)"; then
            install_name_tool \
                -change "$dependency" "@loader_path/$replacement_name" \
                "$destination_path"
        fi
    done < <(otool -L "$source_path" | tail -n +2 | sed -E 's/^[[:space:]]*([^[:space:]]+).*/\1/')
done < "$mapping_file"

while IFS=$'\t' read -r source_path destination_name; do
    source_directory="$(cd -P "$(dirname "$source_path")" && pwd)"
    resolved_source="$source_directory/$(basename "$source_path")"
    formula_root="$(dirname "$source_directory")"
    formula_name="$(basename "$(dirname "$formula_root")")"
    formula_version="$(basename "$formula_root")"
    formula_files="$bookkeeping/formula-files.txt"
    license_paths="$bookkeeping/license-paths.txt"

    validate_field "$formula_name" "Homebrew formula name"
    validate_field "$formula_version" "Homebrew formula version"
    case "$resolved_source" in
    "$formula_root"/*) source_relative="${resolved_source#"$formula_root/"}" ;;
    *)
        echo "Error: Homebrew library is outside its formula root: $resolved_source" >&2
        exit 1
        ;;
    esac

    find "$formula_root/.brew" -maxdepth 1 -type f -name '*.rb' -print \
        | LC_ALL=C sort > "$formula_files"
    if [[ "$(wc -l < "$formula_files" | tr -d '[:space:]')" != "1" ]]; then
        echo "Error: Homebrew formula $formula_name does not contain exactly one formula definition." >&2
        exit 1
    fi
    formula_file="$(sed -n '1p' "$formula_files")"
    formula_sha256="$(shasum -a 256 "$formula_file" | awk '{print $1}')"
    printf '%s\t%s\t%s\t%s\t%s\n' \
        "$destination_name" \
        "$formula_name" \
        "$formula_version" \
        "$source_relative" \
        "$formula_sha256" >> "$manifest_entries"

    if grep -Fqx "$formula_name" "$licensed_formulae"; then
        continue
    fi
    find "$formula_root" -maxdepth 3 -type f \
        \( -iname 'license*' -o -iname 'copying*' \) -print \
        | LC_ALL=C sort > "$license_paths"
    if [[ ! -s "$license_paths" ]]; then
        echo "Error: Homebrew formula $formula_name contains no distributable license file." >&2
        exit 1
    fi
    while IFS= read -r license_path; do
        license_destination="$destination/licenses/$formula_name-$(basename "$license_path")"
        if [[ -e "$license_destination" ]]; then
            echo "Error: duplicate Homebrew license destination: $license_destination" >&2
            exit 1
        fi
        cp "$license_path" "$license_destination"
    done < "$license_paths"
    printf '%s\n' "$formula_name" >> "$licensed_formulae"
done < "$mapping_file"

{
    printf 'schema 1\n'
    printf 'fields library formula version source formula_sha256\n'
    LC_ALL=C sort -u "$manifest_entries"
} > "$destination/dependency-manifest.txt"

library_count="$(wc -l < "$mapping_file" | tr -d '[:space:]')"
printf 'Staged %s macOS runtime libraries in %s\n' "$library_count" "$destination"
