#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
external_dir="$(mktemp -d)"
external_dir="$(cd "${external_dir}" && pwd)"
trap 'rmdir "${external_dir}"' EXIT

# Capture the Docker invocation without building an image. This exercises the
# host-to-container path mapping in build-steam-sniper.sh in a few milliseconds.
docker() {
    printf 'DOCKER_ARG=%s\n' "$@"
}
export -f docker

output="$(
    STEAM_MPV_BUNDLE="${repo_root}/assets" \
    STEAM_MOONLIGHT_BUNDLE="${repo_root}/modules" \
    STEAM_RETROARCH_BUNDLE="${repo_root}/scripts" \
    STEAM_YTDLP_BUNDLE="${repo_root}/packaging" \
    STEAM_RCLONE_BUNDLE="${external_dir}" \
    STEAM_PORTS_BUNDLE="${repo_root}/modules/retro/ports" \
    STEAM_THIRD_PARTY_NOTICES_DIR="${repo_root}/assets" \
        "${repo_root}/scripts/build-steam-sniper.sh"
)"

require_docker_arg() {
    local expected="$1"
    if ! grep -Fxq "DOCKER_ARG=${expected}" <<< "${output}"; then
        echo "Missing Docker argument: ${expected}" >&2
        exit 1
    fi
}

require_docker_arg "STEAM_MPV_BUNDLE=/src/assets"
require_docker_arg "STEAM_MOONLIGHT_BUNDLE=/src/modules"
require_docker_arg "STEAM_RETROARCH_BUNDLE=/src/scripts"
require_docker_arg "STEAM_YTDLP_BUNDLE=/src/packaging"
require_docker_arg "STEAM_PORTS_BUNDLE=/src/modules/retro/ports"
require_docker_arg "STEAM_THIRD_PARTY_NOTICES_DIR=/src/assets"
require_docker_arg "${external_dir}:${external_dir}:ro"
require_docker_arg "STEAM_RCLONE_BUNDLE=${external_dir}"

if grep -Fq "STEAM_MPV_BUNDLE=${repo_root}/assets" <<< "${output}"; then
    echo "A host repository path leaked into the Docker environment." >&2
    exit 1
fi

echo "Steam sniper bundle path mapping OK."
