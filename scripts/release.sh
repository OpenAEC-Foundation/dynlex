#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<USAGE
Usage:
  ./scripts/release.sh
  ./scripts/release.sh <YEAR.WEEK.REVISION>
  ./scripts/release.sh --retag

Behavior:
  - Requires a clean git worktree
  - Verifies compiler, extension and translator versions match
  - Verifies HEAD is already pushed to upstream
  - Verifies CI workflow succeeded for that exact HEAD SHA
  - With no version, uses the current UTC ISO week and increments its revision
  - For a new release:
      - updates compiler, extension and translator version metadata together
      - builds and runs tests locally
      - commits "Release <version>"
      - creates release tag and pushes commit+tag in one push
  - For --retag:
      - keeps all version metadata unchanged
      - force-updates the existing <version> tag to HEAD
  - Waits for GitHub workflows for the pushed release SHA

Notes:
  - If your worktree is not clean, commit first.
  - All package versions must stay synchronized with metadata/VERSION.
  - Requires GitHub CLI auth: gh auth login
  - Override workflow files with:
      RELEASE_CI_WORKFLOW_FILE=<file> RELEASE_WORKFLOW_FILE=<file> ./scripts/release.sh ...
USAGE
}

if [[ $# -gt 1 ]]; then
  usage
  exit 1
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION_FILE="${ROOT_DIR}/metadata/VERSION"
VERSION_TOOL="${ROOT_DIR}/scripts/update-version.mjs"
CI_WORKFLOW_FILE="${RELEASE_CI_WORKFLOW_FILE:-pipeline.yml}"
RELEASE_WORKFLOW_FILE="${RELEASE_WORKFLOW_FILE:-release.yml}"
WORKFLOW_TIMEOUT_SECONDS="${RELEASE_WORKFLOW_TIMEOUT_SECONDS:-1800}"
WORKFLOW_POLL_SECONDS="${RELEASE_WORKFLOW_POLL_SECONDS:-5}"

if [[ ! -f "${VERSION_FILE}" ]]; then
  echo "Error: metadata/VERSION file not found at ${VERSION_FILE}" >&2
  exit 1
fi
if [[ ! -f "${VERSION_TOOL}" ]]; then
  echo "Error: version update tool not found at ${VERSION_TOOL}" >&2
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

if ! command -v node >/dev/null 2>&1; then
  echo "Error: Node.js is required." >&2
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

current_version="$(node "${VERSION_TOOL}" --check)"

mode="${1:-next}"
retag_mode=false
target_version=""
case "${mode}" in
  next)
    target_version="$(node "${VERSION_TOOL}" --next)"
    ;;
  --retag)
    retag_mode=true
    target_version="${current_version}"
    ;;
  *)
    if [[ "${mode}" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]]; then
      target_version="$(node "${VERSION_TOOL}" --validate "${mode}")"
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

release_tag="v${target_version}"
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
if git -C "${ROOT_DIR}" rev-parse -q --verify "refs/tags/${release_tag}" >/dev/null; then
  local_tag_exists=true
fi
if git -C "${ROOT_DIR}" ls-remote --exit-code --tags origin "refs/tags/${release_tag}" >/dev/null 2>&1; then
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
    node "${VERSION_TOOL}" "${current_version}"
  fi
}
trap cleanup EXIT

if [[ "${retag_mode}" != "true" ]]; then
  version_updated=true
  node "${VERSION_TOOL}" "${target_version}"

  echo "Building and testing release ${target_version}..."
  "${ROOT_DIR}/scripts/test.sh" --release

  git -C "${ROOT_DIR}" add metadata/VERSION vscode-extension/package.json vscode-extension/package-lock.json tools/wgsl-translator/Cargo.toml tools/wgsl-translator/Cargo.lock
  git -C "${ROOT_DIR}" commit -m "Release ${target_version}"
  release_commit_created=true
  release_sha="$(git -C "${ROOT_DIR}" rev-parse HEAD)"
fi

if [[ "${retag_mode}" == "true" ]]; then
  git -C "${ROOT_DIR}" tag -f "${release_tag}" "${release_sha}"
  git -C "${ROOT_DIR}" push --force origin "refs/tags/${release_tag}"
else
  git -C "${ROOT_DIR}" tag "${release_tag}" "${release_sha}"
  git -C "${ROOT_DIR}" push origin HEAD "refs/tags/${release_tag}"
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
