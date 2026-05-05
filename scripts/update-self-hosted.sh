#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 OLD_VERSION NEW_VERSION" >&2
    exit 2
fi

old_version="$1"
new_version="$2"

if [[ ! "$old_version" =~ ^v?[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "invalid old self-hosted version: $1" >&2
    exit 2
fi

if [[ ! "$new_version" =~ ^v?[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "invalid new self-hosted version: $2" >&2
    exit 2
fi

file=.github/workflows/e2e.yml

if [[ ! -f "$file" ]]; then
    echo "missing workflow file: $file" >&2
    exit 1
fi

occurrences=$(grep -F -o "$old_version" "$file" | wc -l | tr -d '[:space:]')
if [[ "$occurrences" -eq 0 ]]; then
    echo "self-hosted version $old_version was not found in $file" >&2
    exit 1
fi

OLD_SELF_HOSTED_VERSION="$old_version" NEW_SELF_HOSTED_VERSION="$new_version" perl -0pi -e '
    s/\Q$ENV{OLD_SELF_HOSTED_VERSION}\E/$ENV{NEW_SELF_HOSTED_VERSION}/g
' "$file"
