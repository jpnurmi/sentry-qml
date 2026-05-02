#!/usr/bin/env bash
set -euo pipefail

set_output() {
  if [ -n "${GITHUB_OUTPUT:-}" ]; then
    printf '%s=%s\n' "$1" "$2" >> "$GITHUB_OUTPUT"
  fi
}

image="${SENTRY_SELF_HOSTED_BUNDLE_IMAGE:-}"
if [ -z "$image" ]; then
  image="$(bash .github/scripts/sentry-self-hosted-bundle-image.sh)"
fi

printf 'Restoring preinstalled Sentry self-hosted bundle from %s\n' "$image"

if [[ "$image" == ghcr.io/* && -n "${GITHUB_TOKEN:-}" ]]; then
  printf '%s' "$GITHUB_TOKEN" | docker login ghcr.io -u "${GITHUB_ACTOR:-github-actions}" --password-stdin >/dev/null 2>&1 || true
fi

if ! docker pull "$image"; then
  set_output restored false
  exit 0
fi

bundle_dir=".sentry-self-hosted-bundle"
rm -rf "$bundle_dir" self-hosted
mkdir -p "$bundle_dir"

container_id="$(docker create "$image")"
cleanup() {
  docker rm -f "$container_id" >/dev/null 2>&1 || true
}
trap cleanup EXIT

docker cp "$container_id:/self-hosted.tar.gz" "$bundle_dir/self-hosted.tar.gz"
docker cp "$container_id:/docker-images.tar" "$bundle_dir/docker-images.tar"
docker cp "$container_id:/volumes.txt" "$bundle_dir/volumes.txt"
docker cp "$container_id:/volumes" "$bundle_dir/volumes"

mkdir self-hosted
tar -xzf "$bundle_dir/self-hosted.tar.gz" -C self-hosted
docker load -i "$bundle_dir/docker-images.tar"

while IFS= read -r volume; do
  [ -n "$volume" ] || continue

  docker volume rm "$volume" >/dev/null 2>&1 || true
  docker volume create "$volume" >/dev/null

  mountpoint="$(docker volume inspect --format '{{ .Mountpoint }}' "$volume")"
  sudo tar -xzf "$bundle_dir/volumes/$volume.tar.gz" -C "$mountpoint"
done < "$bundle_dir/volumes.txt"

set_output restored true
