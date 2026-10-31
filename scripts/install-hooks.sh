#!/usr/bin/env bash
set -euo pipefail
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE
root="$(dirname "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")")"
hooks="$(git -C "$root" rev-parse --path-format=absolute --git-path hooks)"
mkdir -p "$hooks"
ln -sf "$root/scripts/pre-push" "$hooks/pre-push"
echo "installed $hooks/pre-push -> scripts/pre-push"
