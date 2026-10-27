#!/usr/bin/env bash
# Usage: ./setup_and_push.sh https://github.com/OWNER/REPOSITORY.git
# The remote must already exist and authentication must already be configured.
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
if [[ $# -ne 1 ]]; then
  echo "Usage: $0 GITHUB_REPOSITORY_URL" >&2
  exit 2
fi
repo_url=$1
if [[ ! "$repo_url" =~ ^https://github\.com/[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+(\.git)?$ &&
      ! "$repo_url" =~ ^git@github\.com:[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+(\.git)?$ ]]; then
  echo "Expected an HTTPS or SSH GitHub repository URL (without credentials)." >&2
  exit 2
fi
command -v git >/dev/null || { echo "git is required" >&2; exit 1; }
if [[ ! -e .git ]]; then
  if git rev-parse --show-toplevel >/dev/null 2>&1; then
    echo "Refusing to initialize inside an existing parent repository. Copy this project outside it first." >&2
    exit 1
  fi
  git init -b main
fi
branch=$(git symbolic-ref --quiet --short HEAD) || {
  echo "Detached HEAD: switch to main first." >&2; exit 1;
}
if [[ "$branch" != main ]]; then
  echo "Current branch is '$branch'; switch to main explicitly before running this script." >&2
  exit 1
fi
if git remote get-url origin >/dev/null 2>&1; then
  if [[ "$(git remote get-url --all origin)" != "$repo_url" || "$(git remote get-url --push --all origin)" != "$repo_url" ]]; then
    echo "Existing origin differs from the requested URL; inspect it before retrying." >&2
    exit 1
  fi
else
  git remote add origin "$repo_url"
fi
if ! git diff --cached --quiet; then
  echo "Pre-existing staged changes found; commit or unstage them before running this script." >&2
  exit 1
fi
# Stage only this project's known files; honor .gitignore. Review before running.
git add -- .github .gitignore include src python tests docs CMakeLists.txt README.md REPORT.md setup_and_push.sh
if ! git diff --cached --quiet; then
  git commit -m "feat: implement hybrid edge telemetry engine"
fi
git rev-parse --verify HEAD >/dev/null
git push --set-upstream origin main # Never force-push or overwrite remote history.
echo "Repository pushed successfully."
