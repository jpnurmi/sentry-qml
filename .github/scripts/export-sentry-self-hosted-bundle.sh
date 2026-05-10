#!/usr/bin/env bash
set -euo pipefail

image="${SENTRY_SELF_HOSTED_BUNDLE_IMAGE:?}"
self_hosted_dir="${1:-self-hosted}"
bundle_dir="${2:-.sentry-self-hosted-bundle}"

rm -rf "$bundle_dir"
mkdir -p "$bundle_dir/volumes"
bundle_dir="$(cd "$bundle_dir" && pwd)"

tar --exclude=.git -czf "$bundle_dir/self-hosted.tar.gz" -C "$self_hosted_dir" .

(
  cd "$self_hosted_dir"
  docker compose --ansi never --env-file .env --env-file .env.custom config --images
) | sort -u > "$bundle_dir/docker-images.txt"

mapfile -t images < "$bundle_dir/docker-images.txt"
docker save -o "$bundle_dir/docker-images.tar" "${images[@]}"

docker volume ls --format '{{.Name}}' | awk '/^sentry-/ { print }' | sort > "$bundle_dir/volumes.txt"

while IFS= read -r volume; do
  [ -n "$volume" ] || continue

  mountpoint="$(docker volume inspect --format '{{ .Mountpoint }}' "$volume")"
  sudo tar -czf "$bundle_dir/volumes/$volume.tar.gz" -C "$mountpoint" .
  sudo chown "$(id -u):$(id -g)" "$bundle_dir/volumes/$volume.tar.gz"
done < "$bundle_dir/volumes.txt"

cat > "$bundle_dir/Dockerfile" <<'EOF'
FROM scratch
COPY self-hosted.tar.gz /self-hosted.tar.gz
COPY docker-images.tar /docker-images.tar
COPY docker-images.txt /docker-images.txt
COPY volumes.txt /volumes.txt
COPY volumes/ /volumes/
CMD ["/bin/true"]
EOF

docker build -t "$image" "$bundle_dir"
