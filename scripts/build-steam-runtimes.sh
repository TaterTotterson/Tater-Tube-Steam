#!/usr/bin/env bash
# Build Linux x86_64 desktop runtimes in Valve's sniper SDK.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
OUTPUT_DIR="${1:-${REPO_ROOT}/../Tater-Tube-Steam-Runtime}"
SNIPER_BUILDER_IMAGE="${STEAM_SNIPER_BUILDER_IMAGE:-tater-tube-steam-sniper-builder}"
RUNTIME_IMAGE="${STEAM_RUNTIME_BUILDER_IMAGE:-tater-tube-steam-runtimes}"
SNIPER_IMAGE="${STEAM_SNIPER_IMAGE:-registry.gitlab.steamos.cloud/steamrt/sniper/sdk@sha256:2969e5a47146a6494c01d953cd818b1d62712f42f9e54c4809d7a3aa8dc276ce}"
BUILD_JOBS="${STEAM_RUNTIME_BUILD_JOBS:-4}"

if ! command -v docker >/dev/null 2>&1; then
    echo "Docker is required to build the Steam runtimes." >&2
    exit 1
fi

docker build \
    --platform linux/amd64 \
    --build-arg "SNIPER_IMAGE=${SNIPER_IMAGE}" \
    -f "${REPO_ROOT}/packaging/steam/Dockerfile.sniper" \
    -t "${SNIPER_BUILDER_IMAGE}" \
    "${REPO_ROOT}"

docker build \
    --platform linux/amd64 \
    --build-arg "SNIPER_BUILDER_IMAGE=${SNIPER_BUILDER_IMAGE}" \
    --build-arg "BUILD_JOBS=${BUILD_JOBS}" \
    -f "${REPO_ROOT}/packaging/steam/Dockerfile.runtimes" \
    -t "${RUNTIME_IMAGE}" \
    "${REPO_ROOT}"

mkdir -p "${OUTPUT_DIR}" "${OUTPUT_DIR}/notices"
for generated_dir in mpv moonlight-sdl retroarch candidate-cores; do
    cmake -E remove_directory "${OUTPUT_DIR}/${generated_dir}"
done

container_id="$(docker create --platform linux/amd64 "${RUNTIME_IMAGE}")"
cleanup() {
    docker rm -f "${container_id}" >/dev/null 2>&1 || true
}
trap cleanup EXIT

for generated_dir in mpv moonlight-sdl retroarch; do
    docker cp \
        "${container_id}:/opt/tater-tube-runtimes/${generated_dir}" \
        "${OUTPUT_DIR}/"
done
docker cp \
    "${container_id}:/opt/tater-tube-runtimes/notices/." \
    "${OUTPUT_DIR}/notices/"

echo "Steam runtime bundles written to ${OUTPUT_DIR}"
echo "Approved RetroArch cores are under ${OUTPUT_DIR}/retroarch/cores."
