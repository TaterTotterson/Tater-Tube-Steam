#!/usr/bin/env bash
# Download and verify the immutable Steam runtime bundle pinned by the repository.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
LOCK_FILE="${STEAM_RUNTIME_BUNDLE_LOCK:-${REPO_ROOT}/packaging/steam/runtime-bundle.lock}"
OUTPUT_DIR="${1:-${REPO_ROOT}/out/steam-runtime}"

if [ ! -f "${LOCK_FILE}" ]; then
    echo "Steam runtime bundle lock not found: ${LOCK_FILE}" >&2
    exit 1
fi

# The lock is maintained in this repository and contains assignments only.
# shellcheck source=/dev/null
source "${LOCK_FILE}"
: "${STEAM_RUNTIME_BUNDLE_TAG:?Missing STEAM_RUNTIME_BUNDLE_TAG in lock}"
: "${STEAM_RUNTIME_BUNDLE_FILE:?Missing STEAM_RUNTIME_BUNDLE_FILE in lock}"
: "${STEAM_RUNTIME_BUNDLE_SHA256:?Missing STEAM_RUNTIME_BUNDLE_SHA256 in lock}"

repository="${STEAM_RUNTIME_BUNDLE_REPOSITORY:-TaterTotterson/Tater-Tube-Steam}"
url="https://github.com/${repository}/releases/download/${STEAM_RUNTIME_BUNDLE_TAG}/${STEAM_RUNTIME_BUNDLE_FILE}"
download_dir="$(mktemp -d)"
archive="${download_dir}/${STEAM_RUNTIME_BUNDLE_FILE}"
cleanup() {
    rm -rf "${download_dir}"
}
trap cleanup EXIT

curl -fL --retry 5 --retry-all-errors --connect-timeout 20 \
    -o "${archive}" "${url}"
if command -v sha256sum >/dev/null 2>&1; then
    echo "${STEAM_RUNTIME_BUNDLE_SHA256}  ${archive}" | sha256sum -c -
else
    actual_sha="$(shasum -a 256 "${archive}" | awk '{ print $1 }')"
    test "${actual_sha}" = "${STEAM_RUNTIME_BUNDLE_SHA256}"
fi
zstd --test "${archive}"

rm -rf "${OUTPUT_DIR}"
mkdir -p "${OUTPUT_DIR}"
tar --zstd -xf "${archive}" -C "${OUTPUT_DIR}"

for required_path in \
    mpv/bin/mpv \
    moonlight-sdl/bin/moonlight \
    retroarch/bin/retroarch \
    ports/sm64coopdx/sm64coopdx \
    ports/2ship2harkinian/2s2h.elf \
    ports/shipwright/soh.elf \
    ports/spaghettikart/Spaghettify \
    ports/starship/starship-bin \
    ports/dusklight/dusklight-bin \
    yt-dlp/bin/yt-dlp \
    rclone/bin/rclone; do
    test -f "${OUTPUT_DIR}/${required_path}"
done

echo "Verified Steam runtime bundle ${STEAM_RUNTIME_BUNDLE_TAG} in ${OUTPUT_DIR}"
