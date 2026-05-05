#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 OLD_VERSION NEW_VERSION" >&2
    exit 2
fi

old_version="${1#v}"
new_version="${2#v}"

if [[ ! "$old_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "invalid old Qt version: $1" >&2
    exit 2
fi

if [[ ! "$new_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "invalid new Qt version: $2" >&2
    exit 2
fi

files=(
    .github/workflows/ci.yml
    .github/workflows/e2e.yml
)

occurrences=0
for file in "${files[@]}"; do
    if [[ ! -f "$file" ]]; then
        echo "missing workflow file: $file" >&2
        exit 1
    fi

    count=$(grep -F -o "$old_version" "$file" | wc -l | tr -d '[:space:]')
    occurrences=$((occurrences + count))
done

if [[ "$occurrences" -eq 0 ]]; then
    echo "Qt version $old_version was not found in workflow files" >&2
    exit 1
fi

OLD_QT_VERSION="$old_version" NEW_QT_VERSION="$new_version" perl -0pi -e '
    s/\Q$ENV{OLD_QT_VERSION}\E/$ENV{NEW_QT_VERSION}/g
' "${files[@]}"
