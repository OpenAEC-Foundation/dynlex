#!/bin/bash

dynlex_min_llvm_version() {
    echo 20
}

dynlex_install_llvm_version() {
    if [ -n "${DYNLEX_LLVM_VERSION:-}" ]; then
        echo "$DYNLEX_LLVM_VERSION"
    else
        dynlex_min_llvm_version
    fi
}

dynlex_detect_installed_llvm_version() {
    if [ -n "${DYNLEX_LLVM_VERSION:-}" ]; then
        echo "$DYNLEX_LLVM_VERSION"
        return 0
    fi

    local min_version
    min_version="$(dynlex_min_llvm_version)"

    local version
    while read -r version; do
        [ -n "$version" ] || continue
        if [ "$version" -ge "$min_version" ] &&
            command -v "llvm-config-$version" >/dev/null 2>&1 &&
            command -v "clang-$version" >/dev/null 2>&1; then
            echo "$version"
            return 0
        fi
    done < <(compgen -c | sed -n 's/^llvm-config-\([0-9][0-9]*\)$/\1/p' | sort -u -nr)

    if command -v llvm-config >/dev/null 2>&1 && command -v clang >/dev/null 2>&1; then
        local llvm_version
        local clang_version

        llvm_version="$(llvm-config --version | cut -d. -f1)"
        clang_version="$(clang --version | sed -n '1 s/.*clang version \([0-9][0-9]*\).*/\1/p')"

        if [ "${llvm_version:-0}" -ge "$min_version" ] && [ "$clang_version" = "$llvm_version" ]; then
            echo "$llvm_version"
            return 0
        fi
    fi

    return 1
}

dynlex_resolve_tool() {
    local base_name="$1"
    local version="$2"

    if command -v "${base_name}-${version}" >/dev/null 2>&1; then
        echo "${base_name}-${version}"
    else
        echo "${base_name}"
    fi
}
