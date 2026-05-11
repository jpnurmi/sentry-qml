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
if docker cp "$container_id:/docker-images.tar.gz" "$bundle_dir/docker-images.tar.gz" 2>/dev/null; then
  docker_images_archive="$bundle_dir/docker-images.tar.gz"
else
  printf 'Using legacy uncompressed Docker image archive...\n'
  docker cp "$container_id:/docker-images.tar" "$bundle_dir/docker-images.tar"
  docker_images_archive="$bundle_dir/docker-images.tar"
fi
docker cp "$container_id:/volumes.txt" "$bundle_dir/volumes.txt"
docker cp "$container_id:/volumes" "$bundle_dir/volumes"
du -h "$bundle_dir"/self-hosted.tar.gz "$docker_images_archive"
volume_archives=("$bundle_dir"/volumes/*.tar.gz)
if [ -e "${volume_archives[0]}" ]; then
  du -h "${volume_archives[@]}"
fi

mkdir self-hosted
printf 'Extracting self-hosted sources...\n'
tar -xzf "$bundle_dir/self-hosted.tar.gz" -C self-hosted

printf 'Loading Docker images from %s...\n' "$docker_images_archive"
docker load -i "$docker_images_archive"

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
