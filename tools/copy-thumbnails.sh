#!/usr/bin/env bash
# Copies the Library's thumbnails made in a test container (library/thumbnails
# beside the TuxAPE unpacked there) into the current folder.
#
#   tools/copy-thumbnails.sh [CONTAINER]
#
# CONTAINER: test-tuxape-ubuntu-22.04 by default. The container may be
# running or stopped.
set -euo pipefail

container=${1:-test-tuxape-ubuntu-22.04}
docker container inspect "$container" >/dev/null 2>&1 \
    || { echo "no container named $container (docker ps -a lists them)" >&2; exit 1; }

# Where TuxAPE was unpacked: the folder of the first tuxape-*/library/thumbnails.
was_running=$(docker container inspect -f '{{.State.Running}}' "$container")
[ "$was_running" = true ] || docker start "$container" >/dev/null
folder=$(docker exec "$container" sh -c \
    'ls -d /root/tuxape-*/library/thumbnails /opt/*/library/thumbnails /home/*/tuxape-*/library/thumbnails 2>/dev/null | head -1' || true)
[ "$was_running" = true ] || docker stop "$container" >/dev/null
[ -n "$folder" ] || { echo "no library/thumbnails in $container" >&2; exit 1; }

docker cp "$container:$folder" ./
echo "$(find ./thumbnails -type f | wc -l) thumbnails from $container:$folder in $(pwd)/thumbnails"
