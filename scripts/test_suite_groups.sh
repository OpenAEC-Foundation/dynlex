#!/bin/bash
# Isolated suite workers retain every result and join every child on failure.

start_test_group() {
    local group="$1"
    local started=$SECONDS
    shift
    (
        trap - EXIT
        passed=0; failed=0; skipped=0; test_output=""; failures=()
        "$@" || exit "$?"
        printf '%s\0' "$passed" "$failed" "$skipped" "$test_output" "${failures[@]+"${failures[@]}"}" > "$TEST_OUTPUT_DIR/$group-results"
        printf 'Test group %s finished in %s seconds (%s passed, %s failed, %s skipped).\n' \
            "$group" "$((SECONDS - started))" "$passed" "$failed" "$skipped" >&2
    ) &
    test_group_worker=$!
}

collect_test_group() {
    local group="$1" worker="$2"
    local worker_status=0
    wait "$worker" || worker_status=$?
    if [[ $worker_status -ne 0 ]]; then
        echo "Test group $group exited with status $worker_status." >&2
        return "$worker_status"
    fi
    local group_passed group_failed group_skipped group_output group_failure
    local group_failures=()
    {
        IFS= read -r -d '' group_passed &&
        IFS= read -r -d '' group_failed &&
        IFS= read -r -d '' group_skipped &&
        IFS= read -r -d '' group_output || return 1
        while IFS= read -r -d '' group_failure; do
            group_failures+=("$group_failure")
        done
    } < "$TEST_OUTPUT_DIR/$group-results" || return 1
    passed=$((passed + group_passed))
    failed=$((failed + group_failed))
    skipped=$((skipped + group_skipped))
    test_output="$group_output$test_output"
    failures=("${group_failures[@]+"${group_failures[@]}"}" "${failures[@]+"${failures[@]}"}")
}

collect_required_tests() {
    local status=0
    collect_test_group fixture "$fixture_worker" || status=$?
    collect_test_group fixture_parallel "$fixture_parallel_worker" || status=$?
    collect_test_group incremental "$incremental_worker" || status=$?
    collect_test_group incremental_dependencies "$incremental_dependencies_worker" || status=$?
    collect_test_group incremental_native "$incremental_native_worker" || status=$?
    return "$status"
}

