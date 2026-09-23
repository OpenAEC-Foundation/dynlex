#!/bin/bash
# Required fixtures can declare a bounded wall-clock compilation budget.
# Ordinary language regressions retain the caller's platform default.
dynlex_fixture_compile_timeout() {
    local fixture_directory="$1"
    local default_seconds="$2"
    local option_file="$fixture_directory/compile_timeout_seconds.txt"
    local seconds="$default_seconds"
    if [[ -f "$option_file" ]]; then
        seconds=$(<"$option_file")
        seconds="${seconds%$'\r'}"
    fi
    if [[ ! "$seconds" =~ ^[1-9][0-9]{0,2}$ ]] || ((seconds > 300)); then
        echo "Invalid compile_timeout_seconds.txt: expected an integer from 1 to 300" >&2
        return 1
    fi
    printf '%s\n' "$seconds"
}
