#!/usr/bin/env bash
set -euo pipefail

ref="${SENTRY_SELF_HOSTED_REF:?}"
profile="${SENTRY_COMPOSE_PROFILES:?}"
repository="${GITHUB_REPOSITORY:?}"

repository="$(printf '%s' "$repository" | tr '[:upper:]' '[:lower:]')"
tag_value="$(printf '%s-%s' "$ref" "$profile" | tr '[:upper:]' '[:lower:]')"
tag_value="$(printf '%s' "$tag_value" | sed -E 's/[^a-z0-9_.-]+/-/g; s/^-+//; s/-+$//')"
tag_value="$(printf 'self-hosted-%s' "$tag_value" | cut -c1-128)"

printf 'ghcr.io/%s/sentry-self-hosted-e2e:%s\n' "$repository" "$tag_value"
