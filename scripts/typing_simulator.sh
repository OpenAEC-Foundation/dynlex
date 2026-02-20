#!/bin/bash
# Typing Simulator — fuzzes the compiler by simulating partially-typed code.
#
# Picks random .dl files (and occasionally non-.dl files), randomly mutates
# them (truncate mid-line, delete random lines, cut off the end, etc.),
# writes the result to build/typing_test.dl, and compiles it. If the compiler
# crashes (segfault, abort, timeout), the script exits and leaves the file
# intact for debugging.
#
# Usage:
#   ./scripts/typing_simulator.sh [--iterations N]

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
COMPILER="$PROJECT_DIR/build/dynlex"
TEST_FILE="$PROJECT_DIR/build/typing_test.dl"
TIMEOUT_SEC=5

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

MAX_ITER=0  # 0 = infinite

while [[ $# -gt 0 ]]; do
    case "$1" in
        --iterations|-n) MAX_ITER="$2"; shift 2 ;;
        *) echo "Usage: $0 [--iterations N]"; exit 1 ;;
    esac
done

if [[ ! -x "$COMPILER" ]]; then
    echo -e "${YELLOW}Compiler not found, building...${NC}"
    "$SCRIPT_DIR/build.sh"
fi

# Gather source files
mapfile -t DL_FILES < <(find "$PROJECT_DIR" -name '*.dl' -not -path '*/build/*' -not -path '*/.git/*' -not -path '*/node_modules/*')
mapfile -t OTHER_FILES < <(find "$PROJECT_DIR" \( -name '*.cpp' -o -name '*.h' -o -name '*.ts' -o -name '*.json' -o -name '*.md' \) -not -path '*/build/*' -not -path '*/.git/*' -not -path '*/node_modules/*' | head -50)

if [[ ${#DL_FILES[@]} -eq 0 ]]; then
    echo "No .dl files found!"
    exit 1
fi

echo -e "${BOLD}Typing Simulator${NC}"
echo "  .dl files:   ${#DL_FILES[@]}"
echo "  other files:  ${#OTHER_FILES[@]}"
echo "  test file:    $TEST_FILE"
echo "  Press Ctrl+C to stop"
echo ""

is_crash() {
    local exit_code=$1
    if [[ $exit_code -eq 124 ]]; then
        echo "timeout"
        return 0
    elif [[ $exit_code -ge 128 ]]; then
        local sig=$((exit_code - 128))
        case $sig in
            6)  echo "SIGABRT" ;;
            8)  echo "SIGFPE" ;;
            11) echo "SIGSEGV" ;;
            *)  echo "signal $sig" ;;
        esac
        return 0
    fi
    return 1
}

# Random int in [0, max)
rand() {
    echo $(( RANDOM % $1 ))
}

# Mutation strategies
mutate_truncate_at_line() {
    # Cut off the file at a random line
    local line_count
    line_count=$(wc -l < "$1")
    [[ $line_count -le 1 ]] && return
    local cut_at=$(( (RANDOM % (line_count - 1)) + 1 ))
    head -n "$cut_at" "$1" > "$1.tmp" && mv "$1.tmp" "$1"
    echo "truncated to $cut_at/$line_count lines"
}

mutate_truncate_mid_line() {
    # Cut off in the middle of a random line (simulates typing)
    local line_count
    line_count=$(wc -l < "$1")
    [[ $line_count -le 1 ]] && return
    local target_line=$(( (RANDOM % line_count) + 1 ))
    local line_content
    line_content=$(sed -n "${target_line}p" "$1")
    local line_len=${#line_content}
    [[ $line_len -le 1 ]] && return
    local cut_at=$(( RANDOM % line_len ))
    local truncated="${line_content:0:$cut_at}"
    # Keep lines before target, write truncated line, drop the rest
    {
        head -n $((target_line - 1)) "$1"
        echo -n "$truncated"
    } > "$1.tmp" && mv "$1.tmp" "$1"
    echo "cut mid-line at line $target_line, char $cut_at"
}

mutate_delete_random_lines() {
    # Delete 1-5 random lines
    local line_count
    line_count=$(wc -l < "$1")
    [[ $line_count -le 2 ]] && return
    local num_del=$(( (RANDOM % 5) + 1 ))
    [[ $num_del -ge $line_count ]] && num_del=$((line_count - 1))
    local lines_to_del=""
    for (( d = 0; d < num_del; d++ )); do
        lines_to_del+="$(( (RANDOM % line_count) + 1 ))d;"
    done
    sed "$lines_to_del" "$1" > "$1.tmp" && mv "$1.tmp" "$1"
    echo "deleted $num_del random lines"
}

mutate_blank_random_lines() {
    # Replace 1-3 random lines with empty lines (broken indentation)
    local line_count
    line_count=$(wc -l < "$1")
    [[ $line_count -le 2 ]] && return
    local num=$(( (RANDOM % 3) + 1 ))
    local cmd=""
    for (( d = 0; d < num; d++ )); do
        local ln=$(( (RANDOM % line_count) + 1 ))
        cmd+="${ln}s/.*//;"
    done
    sed "$cmd" "$1" > "$1.tmp" && mv "$1.tmp" "$1"
    echo "blanked $num random lines"
}

mutate_garble_line() {
    # Insert random characters into a random line
    local line_count
    line_count=$(wc -l < "$1")
    [[ $line_count -le 1 ]] && return
    local target_line=$(( (RANDOM % line_count) + 1 ))
    local chars="abcdefghijklmnopqrstuvwxyz 0123456789@(){}[]#:."
    local garbage=""
    for (( c = 0; c < $(( (RANDOM % 10) + 1 )); c++ )); do
        garbage+="${chars:$(( RANDOM % ${#chars} )):1}"
    done
    sed "${target_line}s/.*/${garbage}/" "$1" > "$1.tmp" && mv "$1.tmp" "$1"
    echo "garbled line $target_line"
}

MUTATIONS=(mutate_truncate_at_line mutate_truncate_mid_line mutate_delete_random_lines mutate_blank_random_lines mutate_garble_line)

iteration=0
crashes=0
clean_errors=0

while true; do
    ((iteration++))
    if [[ $MAX_ITER -gt 0 && $iteration -gt $MAX_ITER ]]; then
        break
    fi

    # Pick a source file: 1 in 100 chance of non-.dl file
    if [[ ${#OTHER_FILES[@]} -gt 0 ]] && [[ $(( RANDOM % 100 )) -eq 0 ]]; then
        src="${OTHER_FILES[$(( RANDOM % ${#OTHER_FILES[@]} ))]}"
        file_type="other"
    else
        src="${DL_FILES[$(( RANDOM % ${#DL_FILES[@]} ))]}"
        file_type="dl"
    fi

    src_label=$(realpath --relative-to="$PROJECT_DIR" "$src" 2>/dev/null || echo "$src")

    # Copy source to test file
    cp "$src" "$TEST_FILE"

    # Apply 1-3 random mutations
    num_mutations=$(( (RANDOM % 3) + 1 ))
    mutation_desc=""
    for (( m = 0; m < num_mutations; m++ )); do
        mut="${MUTATIONS[$(( RANDOM % ${#MUTATIONS[@]} ))]}"
        desc=$($mut "$TEST_FILE" 2>/dev/null)
        [[ -n "$desc" ]] && mutation_desc+="$desc; "
    done

    # Run compiler
    output=$(timeout "$TIMEOUT_SEC" "$COMPILER" "$TEST_FILE" -o "$PROJECT_DIR/build/typing_test.out" 2>&1)
    exit_code=$?

    crash_type=$(is_crash "$exit_code") && {
        ((crashes++))
        echo ""
        echo -e "${RED}${BOLD}CRASH${NC} on iteration $iteration"
        echo -e "  Source:    ${CYAN}$src_label${NC} ($file_type)"
        echo -e "  Mutation:  $mutation_desc"
        echo -e "  Signal:    ${RED}$crash_type${NC} (exit $exit_code)"
        echo -e "  Test file: ${CYAN}$TEST_FILE${NC}"
        echo ""
        echo -e "${BOLD}--- Compiler output ---${NC}"
        echo "$output" | head -20
        echo ""
        echo -e "${BOLD}--- File content ---${NC}"
        cat -n "$TEST_FILE" | head -30
        file_lines=$(wc -l < "$TEST_FILE")
        [[ $file_lines -gt 30 ]] && echo "  ... ($file_lines lines total)"
        echo ""
        echo -e "File left at ${CYAN}$TEST_FILE${NC} for debugging."
        echo -e "Reproduce: ${YELLOW}./build/dynlex $TEST_FILE${NC}"
        echo ""
        echo -e "Stats: $iteration iterations, $crashes crash(es), $clean_errors clean errors"
        exit 1
    }

    if [[ $exit_code -ne 0 ]]; then
        ((clean_errors++))
    fi

    # Progress indicator every 10 iterations
    if [[ $(( iteration % 10 )) -eq 0 ]]; then
        printf "\r  [%d] %d clean errors, 0 crashes — last: %s (%s)    " \
            "$iteration" "$clean_errors" "$src_label" "${mutation_desc:0:40}"
    fi
done

echo ""
echo -e "${GREEN}${BOLD}Done!${NC} $iteration iterations, $crashes crashes, $clean_errors clean errors"
