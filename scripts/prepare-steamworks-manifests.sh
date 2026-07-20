#!/usr/bin/env bash
# Generate private ContentBuilder manifests after Steam assigns numeric IDs.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
TEMPLATE_DIR="${REPO_ROOT}/packaging/steam/steamworks"
OUTPUT_DIR="${STEAMWORKS_MANIFEST_DIR:-${REPO_ROOT}/out/steamworks}"
APP_ID="${STEAM_APP_ID:-${1:-}}"
DEPOT_ID="${STEAM_LINUX_DEPOT_ID:-${2:-}}"

usage() {
    echo "usage: STEAM_APP_ID=<number> STEAM_LINUX_DEPOT_ID=<number> $0" >&2
    echo "   or: $0 <app-id> <linux-depot-id>" >&2
}

if ! [[ "${APP_ID}" =~ ^[0-9]+$ ]] || ! [[ "${DEPOT_ID}" =~ ^[0-9]+$ ]]; then
    usage
    exit 2
fi

mkdir -p "${OUTPUT_DIR}"

sed \
    -e "s/YOUR_APP_ID/${APP_ID}/g" \
    -e "s/YOUR_LINUX_DEPOT_ID/${DEPOT_ID}/g" \
    -e 's/depot_build\.vdf\.example/depot_build.vdf/g' \
    "${TEMPLATE_DIR}/app_build.vdf.example" \
    > "${OUTPUT_DIR}/app_build.vdf"

sed \
    -e "s/YOUR_LINUX_DEPOT_ID/${DEPOT_ID}/g" \
    "${TEMPLATE_DIR}/depot_build.vdf.example" \
    > "${OUTPUT_DIR}/depot_build.vdf"

if grep -R -E 'YOUR_(APP|LINUX_DEPOT)_ID' \
        "${OUTPUT_DIR}/app_build.vdf" "${OUTPUT_DIR}/depot_build.vdf"; then
    echo "Generated manifests still contain placeholders." >&2
    exit 1
fi

echo "Generated preview-mode ContentBuilder manifests in ${OUTPUT_DIR}"
