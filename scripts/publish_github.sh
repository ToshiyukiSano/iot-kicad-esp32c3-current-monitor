#!/usr/bin/env bash
# Publish this documentation as a NEW GitHub repository using GitHub CLI.
# Requires explicit confirmation. Default visibility is private.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
repo="${1:-iot-kicad-esp32c3-current-monitor}"
visibility="${2:---private}"
case "$visibility" in --private|--public) ;; *) echo "usage: $0 [repo-name] [--private|--public]" >&2; exit 2;; esac
for cmd in git gh; do
  command -v "$cmd" >/dev/null || { echo "Missing $cmd. Install it before publishing." >&2; exit 1; }
done
gh auth status >/dev/null || { echo 'Run gh auth login first.' >&2; exit 1; }
if gh repo view "$repo" >/dev/null 2>&1; then
  echo "GitHub repository already exists: $repo. Aborting to avoid overwriting." >&2
  exit 1
fi
if [[ -d .git ]]; then
  echo "This directory already contains a Git repository. Review the current remote before publishing." >&2
  exit 1
fi
printf '\nRepo: %s\nVisibility: %s\n' "$repo" "$visibility"
printf 'Inspect everything for credentials, unsafe recommendations, third-party rights, and unverified results before proceeding.\n'
read -r -p 'Type PUBLISH to initialize the repo and push these files: ' confirm
[[ "$confirm" == PUBLISH ]] || { echo 'Canceled'; exit 0; }
git init
git branch -M main
git add .
git diff --cached --check
printf '\nFiles to publish:\n'
git diff --cached --name-only
read -r -p 'Type CONFIRM to create the commit and GitHub repository: ' second
[[ "$second" == CONFIRM ]] || { echo 'Canceled. Git repository initialized locally; no GitHub upload.'; exit 0; }
git commit -m 'docs: add AI-assisted KiCad IoT hardware development guide'
gh repo create "$repo" "$visibility" --source=. --remote=origin --push
printf '\nPublished repository:\n'
gh repo view --json url -q .url
