#!/usr/bin/env bash
# Build through Valve's official Steam Linux Runtime 3.0 (sniper) SDK.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
IMAGE_NAME="${STEAM_SNIPER_BUILDER_IMAGE:-tater-tube-steam-sniper-builder}"
SNIPER_IMAGE="${STEAM_SNIPER_IMAGE:-registry.gitlab.steamos.cloud/steamrt/sniper/sdk@sha256:2969e5a47146a6494c01d953cd818b1d62712f42f9e54c4809d7a3aa8dc276ce}"
SOURCE_COMMIT="$(git -C "${REPO_ROOT}" rev-parse HEAD)"
if git -C "${REPO_ROOT}" diff --quiet \
        && git -C "${REPO_ROOT}" diff --cached --quiet; then
    SOURCE_STATE=clean
else
    SOURCE_STATE=dirty-development-build
fi

if ! command -v docker >/dev/null 2>&1; then
    echo "Docker is required to build in the Steam sniper SDK." >&2
    exit 1
fi

docker build \
    --platform linux/amd64 \
    --build-arg "SNIPER_IMAGE=${SNIPER_IMAGE}" \
    -f "${REPO_ROOT}/packaging/steam/Dockerfile.sniper" \
    -t "${IMAGE_NAME}" \
    "${REPO_ROOT}"

docker_args=(
    run
    --rm
    --platform linux/amd64
    --entrypoint /bin/bash
    --user "$(id -u):$(id -g)"
    -e HOME=/tmp/tater-tube-home
    -e CI=1
    -e STEAM_BUILD_DIR=/tmp/tater-tube-build
    -e STEAM_OUTPUT_DIR=/src/out/steam
    -e STEAM_REQUIRE_COMPLETE_RUNTIME="${STEAM_REQUIRE_COMPLETE_RUNTIME:-0}"
    -e STEAM_REQUIRE_PORTABLE_DEPOT="${STEAM_REQUIRE_PORTABLE_DEPOT:-1}"
    -e STEAM_RELEASE="${STEAM_RELEASE:-0}"
    -e STEAM_SOURCE_COMMIT="${SOURCE_COMMIT}"
    -e STEAM_SOURCE_STATE="${SOURCE_STATE}"
    -v "${REPO_ROOT}:/src"
)

for variable in \
    STEAM_MPV_BUNDLE \
    STEAM_MOONLIGHT_BUNDLE \
    STEAM_RETROARCH_BUNDLE \
    STEAM_YTDLP_BUNDLE \
    STEAM_RCLONE_BUNDLE \
    STEAM_PORTS_BUNDLE \
    STEAM_THIRD_PARTY_NOTICES_DIR; do
    value="${!variable:-}"
    if [ -z "${value}" ]; then
        continue
    fi
    if [ ! -e "${value}" ]; then
        echo "${variable} does not exist: ${value}" >&2
        exit 1
    fi
    absolute_value="$(cd "$(dirname "${value}")" && pwd)/$(basename "${value}")"
    container_value="${absolute_value}"
    if [[ "${absolute_value}" == "${REPO_ROOT}"/* ]]; then
        # The repository is mounted at /src, so translate bundle paths that
        # live under it instead of leaking the host runner path into Docker.
        container_value="/src/${absolute_value#"${REPO_ROOT}/"}"
    else
        docker_args+=(-v "${absolute_value}:${absolute_value}:ro")
    fi
    docker_args+=(-e "${variable}=${container_value}")
done

docker_args+=(
    "${IMAGE_NAME}"
    -lc
    "mkdir -p \"\$HOME\" && ./scripts/build-steam-linux.sh"
)

docker "${docker_args[@]}"
