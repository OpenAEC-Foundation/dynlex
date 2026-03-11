#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<USAGE
Usage:
  ./scripts/release.sh patch
  ./scripts/release.sh minor
  ./scripts/release.sh major
  ./scripts/release.sh <X.Y.Z>
  ./scripts/release.sh --retag

Behavior:
  - Requires a clean git worktree
  - Reads current version from metadata/VERSION
  - Verifies HEAD is already pushed to upstream
  - Verifies CI workflow succeeded for that exact HEAD SHA
  - For patch/minor/major/X.Y.Z:
      - updates metadata/VERSION
      - builds and runs tests locally
      - commits "Release <version>"
      - creates release tag and pushes commit+tag in one push
  - For --retag:
      - keeps metadata/VERSION unchanged
      - force-updates the existing <version> tag to HEAD
  - Waits for GitHub workflows for the pushed release SHA

Notes:
  - If your worktree is not clean, commit first.
  - Compiler/release version source of truth is metadata/VERSION.
  - Requires GitHub CLI auth: gh auth login
  - Override workflow files with:
      RELEASE_CI_WORKFLOW_FILE=<file> RELEASE_WORKFLOW_FILE=<file> ./scripts/release.sh ...
USAGE
}

if [[ $# -ne 1 ]]; then
  usage
  exit 1
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION_FILE="${ROOT_DIR}/metadata/VERSION"
CI_WORKFLOW_FILE="${RELEASE_CI_WORKFLOW_FILE:-ci.yml}"
RELEASE_WORKFLOW_FILE="${RELEASE_WORKFLOW_FILE:-release.yml}"
WORKFLOW_TIMEOUT_SECONDS="${RELEASE_WORKFLOW_TIMEOUT_SECONDS:-1800}"
WORKFLOW_POLL_SECONDS="${RELEASE_WORKFLOW_POLL_SECONDS:-5}"

if [[ ! -f "${VERSION_FILE}" ]]; then
  echo "Error: metadata/VERSION file not found at ${VERSION_FILE}" >&2
  exit 1
fi

if [[ "$(git -C "${ROOT_DIR}" rev-parse --is-inside-work-tree 2>/dev/null || true)" != "true" ]]; then
  echo "Error: not inside a git repository." >&2
  exit 1
fi

if ! git -C "${ROOT_DIR}" diff --quiet || ! git -C "${ROOT_DIR}" diff --cached --quiet || [[ -n "$(git -C "${ROOT_DIR}" ls-files --others --exclude-standard)" ]]; then
  echo "Error: git worktree is not clean. Commit your changes first." >&2
  exit 1
fi

if ! command -v gh >/dev/null 2>&1; then
  echo "Error: GitHub CLI (gh) is required." >&2
  exit 1
fi
if ! gh auth status >/dev/null 2>&1; then
  echo "Error: gh is not authenticated. Run 'gh auth login'." >&2
  exit 1
fi

current_version="$(tr -d '[:space:]' < "${VERSION_FILE}")"
if [[ ! "${current_version}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "Error: invalid version in metadata/VERSION: '${current_version}'" >&2
  exit 1
fi

mode="$1"
retag_mode=false
target_version=""
case "${mode}" in
  patch|minor|major)
    IFS='.' read -r major minor patch <<< "${current_version}"
    case "${mode}" in
      patch) target_version="${major}.${minor}.$((patch + 1))" ;;
      minor) target_version="${major}.$((minor + 1)).0" ;;
      major) target_version="$((major + 1)).0.0" ;;
    esac
    ;;
  --retag)
    retag_mode=true
    target_version="${current_version}"
    ;;
  *)
    if [[ "${mode}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
      target_version="${mode}"
    else
      echo "Error: invalid argument '${mode}'" >&2
      usage
      exit 1
    fi
    ;;
esac

if [[ "${retag_mode}" != "true" && "${target_version}" == "${current_version}" ]]; then
  echo "Error: target version equals current version (${current_version}). Use --retag to move the existing tag." >&2
  exit 1
fi

head_sha="$(git -C "${ROOT_DIR}" rev-parse HEAD)"
upstream_ref="$(git -C "${ROOT_DIR}" rev-parse --abbrev-ref --symbolic-full-name '@{u}' 2>/dev/null || true)"
if [[ -z "${upstream_ref}" ]]; then
  echo "Error: current branch has no upstream. Push branch first." >&2
  exit 1
fi
upstream_sha="$(git -C "${ROOT_DIR}" rev-parse "${upstream_ref}")"
if [[ "${head_sha}" != "${upstream_sha}" ]]; then
  echo "Error: HEAD (${head_sha}) is not the upstream tip (${upstream_ref} -> ${upstream_sha}). Push your commit first." >&2
  exit 1
fi

repo="$(gh repo view --json nameWithOwner --jq '.nameWithOwner')"

wait_for_workflow_for_sha() {
  local workflow_file="$1"
  local target_sha="$2"
  local run_id=""
  local deadline=$((SECONDS + WORKFLOW_TIMEOUT_SECONDS))

  while [[ ${SECONDS} -lt ${deadline} ]]; do
    run_id="$(gh run list \
      --repo "${repo}" \
      --workflow "${workflow_file}" \
      --event push \
      --json databaseId,headSha \
      --limit 100 \
      --jq ".[] | select(.headSha == \"${target_sha}\") | .databaseId" | head -n 1 || true)"
    if [[ -n "${run_id}" ]]; then
      echo "${run_id}"
      return 0
    fi
    sleep "${WORKFLOW_POLL_SECONDS}"
  done

  return 1
}

echo "Verifying CI workflow '${CI_WORKFLOW_FILE}' for HEAD ${head_sha}..."
ci_base_run_id="$(wait_for_workflow_for_sha "${CI_WORKFLOW_FILE}" "${head_sha}" || true)"
if [[ -z "${ci_base_run_id}" ]]; then
  echo "Error: timed out waiting for CI workflow run discovery for commit ${head_sha}." >&2
  exit 1
fi
gh run watch "${ci_base_run_id}" --repo "${repo}" --exit-status
echo "CI workflow run ${ci_base_run_id} succeeded."

local_tag_exists=false
remote_tag_exists=false
if git -C "${ROOT_DIR}" rev-parse -q --verify "refs/tags/${target_version}" >/dev/null; then
  local_tag_exists=true
fi
if git -C "${ROOT_DIR}" ls-remote --exit-code --tags origin "refs/tags/${target_version}" >/dev/null 2>&1; then
  remote_tag_exists=true
fi

if [[ "${retag_mode}" != "true" ]]; then
  if [[ "${local_tag_exists}" == "true" ]]; then
    echo "Error: local tag '${target_version}' already exists. Use --retag to replace it." >&2
    exit 1
  fi
  if [[ "${remote_tag_exists}" == "true" ]]; then
    echo "Error: remote tag '${target_version}' already exists on origin. Use --retag to replace it." >&2
    exit 1
  fi
fi

release_sha="${head_sha}"
version_updated=false
release_commit_created=false
cleanup() {
  if [[ "${version_updated}" == "true" && "${release_commit_created}" != "true" ]]; then
    echo "${current_version}" > "${VERSION_FILE}"
  fi
}
trap cleanup EXIT

if [[ "${retag_mode}" != "true" ]]; then
  echo "${target_version}" > "${VERSION_FILE}"
  version_updated=true

  echo "Building release ${target_version}..."
  "${ROOT_DIR}/scripts/build.sh" --release

  echo "Running tests for release ${target_version}..."
  "${ROOT_DIR}/scripts/run_tests.sh"

  git -C "${ROOT_DIR}" add metadata/VERSION
  git -C "${ROOT_DIR}" commit -m "Release ${target_version}"
  release_commit_created=true
  release_sha="$(git -C "${ROOT_DIR}" rev-parse HEAD)"
fi

if [[ "${retag_mode}" == "true" ]]; then
  git -C "${ROOT_DIR}" tag -f "${target_version}" "${release_sha}"
  git -C "${ROOT_DIR}" push --force origin "refs/tags/${target_version}"
else
  git -C "${ROOT_DIR}" tag "${target_version}" "${release_sha}"
  git -C "${ROOT_DIR}" push origin HEAD "refs/tags/${target_version}"
fi

if [[ "${retag_mode}" != "true" ]]; then
  echo "Verifying CI workflow '${CI_WORKFLOW_FILE}' for release commit ${release_sha}..."
  ci_release_run_id="$(wait_for_workflow_for_sha "${CI_WORKFLOW_FILE}" "${release_sha}" || true)"
  if [[ -z "${ci_release_run_id}" ]]; then
    echo "Error: timed out waiting for CI workflow run discovery for release commit ${release_sha}." >&2
    exit 1
  fi
  gh run watch "${ci_release_run_id}" --repo "${repo}" --exit-status
  echo "CI workflow run ${ci_release_run_id} succeeded."
fi

echo "Waiting for Release workflow '${RELEASE_WORKFLOW_FILE}' for ${release_sha}..."
release_run_id="$(wait_for_workflow_for_sha "${RELEASE_WORKFLOW_FILE}" "${release_sha}" || true)"
if [[ -z "${release_run_id}" ]]; then
  echo "Error: timed out waiting for Release workflow run discovery for commit ${release_sha}." >&2
  exit 1
fi
gh run watch "${release_run_id}" --repo "${repo}" --exit-status
echo "Release workflow run ${release_run_id} succeeded."
echo "Released ${target_version}"
