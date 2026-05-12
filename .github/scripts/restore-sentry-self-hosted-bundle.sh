#!/usr/bin/env bash
set -euo pipefail

set_output() {
  if [ -n "${GITHUB_OUTPUT:-}" ]; then
    printf '%s=%s\n' "$1" "$2" >> "$GITHUB_OUTPUT"
  fi
}

run_with_heartbeat() {
  local message="$1"
  shift

  (
    while true; do
      sleep 30
      printf '%s\n' "$message"
    done
  ) &
  local heartbeat_pid=$!

  local status=0
  "$@" || status=$?

  kill "$heartbeat_pid" >/dev/null 2>&1 || true
  wait "$heartbeat_pid" >/dev/null 2>&1 || true
  return "$status"
}

load_docker_images() {
  local archive_path="$1"
  local archive_name="$2"

  set -o pipefail
  docker cp "$container_id:$archive_path" - |
    tar -xOf - "$archive_name" |
    docker load
}

image="${SENTRY_SELF_HOSTED_BUNDLE_IMAGE:-}"
if [ -z "$image" ]; then
  image="$(bash .github/scripts/sentry-self-hosted-bundle-image.sh)"
fi
set_output image "$image"

printf 'Restoring preinstalled Sentry self-hosted bundle from %s\n' "$image"

if [[ "$image" == ghcr.io/* && -n "${GITHUB_TOKEN:-}" ]]; then
  printf '%s' "$GITHUB_TOKEN" | docker login ghcr.io -u "${GITHUB_ACTOR:-github-actions}" --password-stdin >/dev/null 2>&1 || true
fi

printf 'Pulling bundle image...\n'
if ! docker pull "$image"; then
  set_output restored false
  exit 0
fi

bundle_dir=".sentry-self-hosted-bundle"
rm -rf "$bundle_dir" self-hosted
mkdir -p "$bundle_dir"

printf 'Creating bundle container...\n'
container_id="$(docker create "$image")"
cleanup() {
  docker rm -f "$container_id" >/dev/null 2>&1 || true
}
trap cleanup EXIT

printf 'Copying bundle files...\n'
docker cp "$container_id:/self-hosted.tar.gz" "$bundle_dir/self-hosted.tar.gz"
docker cp "$container_id:/docker-images.txt" "$bundle_dir/docker-images.txt" 2>/dev/null || : > "$bundle_dir/docker-images.txt"
docker cp "$container_id:/volumes.txt" "$bundle_dir/volumes.txt"
docker cp "$container_id:/volumes" "$bundle_dir/volumes"
du -h "$bundle_dir"/self-hosted.tar.gz
volume_archives=("$bundle_dir"/volumes/*.tar.gz)
if [ -e "${volume_archives[0]}" ]; then
  du -h "${volume_archives[@]}"
fi

mkdir self-hosted
printf 'Extracting self-hosted sources...\n'
tar -xzf "$bundle_dir/self-hosted.tar.gz" -C self-hosted

if [ -s "$bundle_dir/docker-images.txt" ]; then
  printf 'Streaming Docker images into Docker...\n'
  if ! run_with_heartbeat 'Still loading Docker images...' \
    load_docker_images /docker-images.tar.gz docker-images.tar.gz; then
    printf 'Compressed Docker image archive failed; trying legacy uncompressed archive...\n'
    run_with_heartbeat 'Still loading Docker images...' \
      load_docker_images /docker-images.tar docker-images.tar
  fi
  set_output images_restored true
else
  printf 'Bundle does not include Docker images; compose images will be pulled later.\n'
  set_output images_restored false
fi

while IFS= read -r volume; do
  [ -n "$volume" ] || continue

  printf 'Restoring Docker volume %s...\n' "$volume"
  docker volume rm "$volume" >/dev/null 2>&1 || true
  docker volume create "$volume" >/dev/null

  mountpoint="$(docker volume inspect --format '{{ .Mountpoint }}' "$volume")"
  sudo tar -xzf "$bundle_dir/volumes/$volume.tar.gz" -C "$mountpoint"
done < "$bundle_dir/volumes.txt"

printf 'Restored preinstalled Sentry self-hosted bundle.\n'
set_output restored true
