#!/usr/bin/env bash
# Build Linux x86_64 desktop runtimes in Valve's sniper SDK.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
OUTPUT_DIR="${1:-${REPO_ROOT}/../Tater-Tube-Steam-Runtime}"
SNIPER_BUILDER_IMAGE="${STEAM_SNIPER_BUILDER_IMAGE:-tater-tube-steam-sniper-builder}"
RUNTIME_IMAGE="${STEAM_RUNTIME_BUILDER_IMAGE:-tater-tube-steam-runtimes}"
SM64_PORT_IMAGE="${STEAM_SM64_PORT_BUILDER_IMAGE:-${STEAM_PORT_BUILDER_IMAGE:-tater-tube-sm64coopdx-steamos-builder}}"
TWO_SHIP_PORT_IMAGE="${STEAM_TWO_SHIP_PORT_BUILDER_IMAGE:-tater-tube-2ship2harkinian-steamos-builder}"
SHIPWRIGHT_PORT_IMAGE="${STEAM_SHIPWRIGHT_PORT_BUILDER_IMAGE:-tater-tube-shipwright-steamos-builder}"
SPAGHETTIKART_PORT_IMAGE="${STEAM_SPAGHETTIKART_PORT_BUILDER_IMAGE:-tater-tube-spaghettikart-steamos-builder}"
STARSHIP_PORT_IMAGE="${STEAM_STARSHIP_PORT_BUILDER_IMAGE:-tater-tube-starship-steamos-builder}"
DUSKLIGHT_PORT_IMAGE="${STEAM_DUSKLIGHT_PORT_BUILDER_IMAGE:-tater-tube-dusklight-steamos-builder}"
SNIPER_IMAGE="${STEAM_SNIPER_IMAGE:-registry.gitlab.steamos.cloud/steamrt/sniper/sdk@sha256:2969e5a47146a6494c01d953cd818b1d62712f42f9e54c4809d7a3aa8dc276ce}"
BUILD_JOBS="${STEAM_RUNTIME_BUILD_JOBS:-4}"
SHIPWRIGHT_BUILD_JOBS="${STEAM_SHIPWRIGHT_BUILD_JOBS:-3}"
SPAGHETTIKART_BUILD_JOBS="${STEAM_SPAGHETTIKART_BUILD_JOBS:-3}"
STARSHIP_BUILD_JOBS="${STEAM_STARSHIP_BUILD_JOBS:-3}"
DUSKLIGHT_BUILD_JOBS="${STEAM_DUSKLIGHT_BUILD_JOBS:-3}"

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
    --build-arg "BUILD_JOBS=${SPAGHETTIKART_BUILD_JOBS}" \
    -f "${REPO_ROOT}/packaging/ports/spaghettikart/Dockerfile.steamos" \
    -t "${SPAGHETTIKART_PORT_IMAGE}" \
    "${REPO_ROOT}"

docker build \
    --platform linux/amd64 \
    --build-arg "SNIPER_BUILDER_IMAGE=${SNIPER_BUILDER_IMAGE}" \
    --build-arg "BUILD_JOBS=${STARSHIP_BUILD_JOBS}" \
    -f "${REPO_ROOT}/packaging/ports/starship/Dockerfile.steamos" \
    -t "${STARSHIP_PORT_IMAGE}" \
    "${REPO_ROOT}"

docker build \
    --platform linux/amd64 \
    --build-arg "SNIPER_BUILDER_IMAGE=${SNIPER_BUILDER_IMAGE}" \
    --build-arg "BUILD_JOBS=${DUSKLIGHT_BUILD_JOBS}" \
    -f "${REPO_ROOT}/packaging/ports/dusklight/Dockerfile.steamos" \
    -t "${DUSKLIGHT_PORT_IMAGE}" \
    "${REPO_ROOT}"

docker build \
    --platform linux/amd64 \
    -f "${REPO_ROOT}/packaging/ports/sm64coopdx/Dockerfile.steamos" \
    -t "${SM64_PORT_IMAGE}" \
    "${REPO_ROOT}"

docker build \
    --platform linux/amd64 \
    --build-arg "SNIPER_BUILDER_IMAGE=${SNIPER_BUILDER_IMAGE}" \
    --build-arg "BUILD_JOBS=${BUILD_JOBS}" \
    -f "${REPO_ROOT}/packaging/ports/2ship2harkinian/Dockerfile.steamos" \
    -t "${TWO_SHIP_PORT_IMAGE}" \
    "${REPO_ROOT}"

docker build \
    --platform linux/amd64 \
    --build-arg "SNIPER_BUILDER_IMAGE=${SNIPER_BUILDER_IMAGE}" \
    --build-arg "BUILD_JOBS=${SHIPWRIGHT_BUILD_JOBS}" \
    -f "${REPO_ROOT}/packaging/ports/shipwright/Dockerfile.steamos" \
    -t "${SHIPWRIGHT_PORT_IMAGE}" \
    "${REPO_ROOT}"

docker build \
    --platform linux/amd64 \
    --build-arg "SNIPER_BUILDER_IMAGE=${SNIPER_BUILDER_IMAGE}" \
    --build-arg "BUILD_JOBS=${BUILD_JOBS}" \
    -f "${REPO_ROOT}/packaging/steam/Dockerfile.runtimes" \
    -t "${RUNTIME_IMAGE}" \
    "${REPO_ROOT}"

mkdir -p "${OUTPUT_DIR}" "${OUTPUT_DIR}/notices"
for generated_dir in mpv moonlight-sdl retroarch ports candidate-cores; do
    cmake -E remove_directory "${OUTPUT_DIR}/${generated_dir}"
done

container_id="$(docker create --platform linux/amd64 "${RUNTIME_IMAGE}")"
sm64_port_container_id="$(docker create --platform linux/amd64 "${SM64_PORT_IMAGE}")"
two_ship_port_container_id="$(docker create --platform linux/amd64 "${TWO_SHIP_PORT_IMAGE}")"
shipwright_port_container_id="$(docker create --platform linux/amd64 "${SHIPWRIGHT_PORT_IMAGE}")"
spaghettikart_port_container_id="$(docker create --platform linux/amd64 "${SPAGHETTIKART_PORT_IMAGE}")"
starship_port_container_id="$(docker create --platform linux/amd64 "${STARSHIP_PORT_IMAGE}")"
dusklight_port_container_id="$(docker create --platform linux/amd64 "${DUSKLIGHT_PORT_IMAGE}")"
cleanup() {
    docker rm -f "${container_id}" >/dev/null 2>&1 || true
    docker rm -f "${sm64_port_container_id}" >/dev/null 2>&1 || true
    docker rm -f "${two_ship_port_container_id}" >/dev/null 2>&1 || true
    docker rm -f "${shipwright_port_container_id}" >/dev/null 2>&1 || true
    docker rm -f "${spaghettikart_port_container_id}" >/dev/null 2>&1 || true
    docker rm -f "${starship_port_container_id}" >/dev/null 2>&1 || true
    docker rm -f "${dusklight_port_container_id}" >/dev/null 2>&1 || true
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
docker cp \
    "${sm64_port_container_id}:/opt/tater-tube-runtimes/ports" \
    "${OUTPUT_DIR}/"
docker cp \
    "${two_ship_port_container_id}:/opt/tater-tube-runtimes/ports/2ship2harkinian" \
    "${OUTPUT_DIR}/ports/"
docker cp \
    "${shipwright_port_container_id}:/opt/tater-tube-runtimes/ports/shipwright" \
    "${OUTPUT_DIR}/ports/"
docker cp \
    "${spaghettikart_port_container_id}:/opt/tater-tube-runtimes/ports/spaghettikart" \
    "${OUTPUT_DIR}/ports/"
docker cp \
    "${starship_port_container_id}:/opt/tater-tube-runtimes/ports/starship" \
    "${OUTPUT_DIR}/ports/"
docker cp \
    "${dusklight_port_container_id}:/opt/tater-tube-runtimes/ports/dusklight" \
    "${OUTPUT_DIR}/ports/"

echo "Steam runtime bundles written to ${OUTPUT_DIR}"
echo "Approved RetroArch cores are under ${OUTPUT_DIR}/retroarch/cores."
echo "Bundled game port engines are under ${OUTPUT_DIR}/ports."
