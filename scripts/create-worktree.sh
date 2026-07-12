#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

if [[ $# -lt 1 || $# -gt 2 ]]; then
    echo "Usage: ./scripts/create-worktree.sh <name> [start-point]" >&2
    exit 1
fi

NAME="$1"
START_POINT="${2:-HEAD}"

if [[ -z "$NAME" || "$NAME" == "." || "$NAME" == ".." || "$NAME" == */* ]]; then
    echo "Worktree name must be a single non-empty path component: $NAME" >&2
    exit 1
fi

if ! git -C "$PROJECT_DIR" check-ref-format --branch "$NAME" >/dev/null 2>&1; then
    echo "Worktree name is not a valid branch name: $NAME" >&2
    exit 1
fi

WORKTREES_DIR="$PROJECT_DIR/.worktrees"
WORKTREE_DIR="$WORKTREES_DIR/$NAME"
mkdir -p "$WORKTREES_DIR"

if [[ -e "$WORKTREE_DIR" ]]; then
    echo "Worktree path already exists: $WORKTREE_DIR" >&2
    exit 1
fi

if git -C "$PROJECT_DIR" show-ref --verify --quiet "refs/heads/$NAME"; then
    if [[ $# -eq 2 ]]; then
        echo "Start point cannot be supplied when branch '$NAME' already exists." >&2
        exit 1
    fi
    git -C "$PROJECT_DIR" worktree add "$WORKTREE_DIR" "$NAME"
else
    git -C "$PROJECT_DIR" worktree add -b "$NAME" "$WORKTREE_DIR" "$START_POINT"
fi

echo "$WORKTREE_DIR"
