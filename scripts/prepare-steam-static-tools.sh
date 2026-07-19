#!/usr/bin/env bash
# Fetch pinned official x86_64 builds of the static Steam helper tools.
set -euo pipefail

OUTPUT_ROOT="${1:-}"
if [ -z "${OUTPUT_ROOT}" ]; then
    echo "usage: $0 <runtime-output-directory>" >&2
    exit 2
fi

RCLONE_VERSION="${RCLONE_VERSION:-v1.74.4}"
RCLONE_SHA256="${RCLONE_SHA256:-fe435e0c36228e7c2f116a8701f01127bb1f694005fc11d1f27186c8bca4115d}"
YTDLP_VERSION="${YTDLP_VERSION:-2026.07.04}"
YTDLP_SHA256="${YTDLP_SHA256:-6bbb3d314cde4febe36e5fa1d55462e29c974f63444e707871834f6d8cc210ae}"

for command in curl unzip; do
    if ! command -v "${command}" >/dev/null 2>&1; then
        echo "Missing required command: ${command}" >&2
        exit 1
    fi
done

verify_sha256() {
    local expected="$1"
    local file="$2"
    if command -v sha256sum >/dev/null 2>&1; then
        echo "${expected}  ${file}" | sha256sum -c -
    else
        local actual
        actual="$(shasum -a 256 "${file}" | awk '{print $1}')"
        if [ "${actual}" != "${expected}" ]; then
            echo "SHA-256 mismatch for ${file}" >&2
            exit 1
        fi
    fi
}

work_dir="$(mktemp -d "${TMPDIR:-/tmp}/tater-tube-static-tools.XXXXXX")"
trap 'rm -rf "${work_dir}"' EXIT
rclone_archive="${work_dir}/rclone.zip"
ytdlp_binary="${work_dir}/yt-dlp"

curl -fL --retry 3 \
    "https://github.com/rclone/rclone/releases/download/${RCLONE_VERSION}/rclone-${RCLONE_VERSION}-linux-amd64.zip" \
    -o "${rclone_archive}"
verify_sha256 "${RCLONE_SHA256}" "${rclone_archive}"
unzip -q "${rclone_archive}" -d "${work_dir}/rclone"

mkdir -p "${OUTPUT_ROOT}/rclone/bin"
cp "${work_dir}/rclone/rclone-${RCLONE_VERSION}-linux-amd64/rclone" \
    "${OUTPUT_ROOT}/rclone/bin/rclone"
chmod +x "${OUTPUT_ROOT}/rclone/bin/rclone"

curl -fL --retry 3 \
    "https://github.com/yt-dlp/yt-dlp/releases/download/${YTDLP_VERSION}/yt-dlp_linux" \
    -o "${ytdlp_binary}"
verify_sha256 "${YTDLP_SHA256}" "${ytdlp_binary}"
mkdir -p "${OUTPUT_ROOT}/yt-dlp/bin"
cp "${ytdlp_binary}" "${OUTPUT_ROOT}/yt-dlp/bin/yt-dlp"
chmod +x "${OUTPUT_ROOT}/yt-dlp/bin/yt-dlp"

mkdir -p "${OUTPUT_ROOT}/notices"
{
    echo "rclone ${RCLONE_VERSION}"
    echo "Source: https://github.com/rclone/rclone/tree/${RCLONE_VERSION}"
    echo
    curl -fsSL "https://raw.githubusercontent.com/rclone/rclone/${RCLONE_VERSION}/COPYING"
} > "${OUTPUT_ROOT}/notices/rclone.txt"
{
    echo "yt-dlp ${YTDLP_VERSION}"
    echo "Source: https://github.com/yt-dlp/yt-dlp/tree/${YTDLP_VERSION}"
    echo
    curl -fsSL "https://raw.githubusercontent.com/yt-dlp/yt-dlp/${YTDLP_VERSION}/LICENSE"
} > "${OUTPUT_ROOT}/notices/yt-dlp.txt"

{
    echo "rclone ${RCLONE_VERSION} ${RCLONE_SHA256}"
    echo "yt-dlp ${YTDLP_VERSION} ${YTDLP_SHA256}"
} > "${OUTPUT_ROOT}/STATIC-TOOLS.lock"

echo "Pinned Steam tools prepared at ${OUTPUT_ROOT}"
echo "STEAM_RCLONE_BUNDLE=${OUTPUT_ROOT}/rclone"
echo "STEAM_YTDLP_BUNDLE=${OUTPUT_ROOT}/yt-dlp"
