#!/bin/bash
set -euo pipefail

if [[ "$GITHUB_REF_TYPE" == "branch" ]]; then
    if [[ "$GITHUB_REF" != "refs/heads/$DEFAULT_BRANCH" ]]; then
        echo "Release validation is only allowed from $DEFAULT_BRANCH or a release tag." >&2
        exit 1
    fi
    echo "Validating release builds without publication."
elif [[ "$GITHUB_REF_TYPE" == "tag" ]]; then
    tag="${GITHUB_REF#refs/tags/}"
    tag_version="${tag#v}"
    file_version="$(tr -d '[:space:]' < metadata/VERSION)"
    if [[ "$tag_version" != "$file_version" ]]; then
        echo "Tag version ($tag_version) does not match metadata/VERSION ($file_version)." >&2
        exit 1
    fi
else
    echo "Unsupported release ref: $GITHUB_REF" >&2
    exit 1
fi

if ! git merge-base --is-ancestor HEAD "refs/remotes/origin/$DEFAULT_BRANCH"; then
    echo "Release commit must already be merged into $DEFAULT_BRANCH." >&2
    exit 1
fi
