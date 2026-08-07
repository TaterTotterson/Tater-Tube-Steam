#!/usr/bin/env bash
# Validate the artwork required by the non-Steam library shortcut.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
ASSET_DIR="${1:-${REPO_ROOT}/assets/steam/library/generated}"
failures=0

if ! command -v ffprobe >/dev/null 2>&1; then
    echo "ffprobe is required to validate the Steam library artwork." >&2
    exit 1
fi

fail() {
    echo "ERROR: $*" >&2
    failures=$((failures + 1))
}

check_dimensions() {
    local name="$1"
    local expected="$2"
    local path="${ASSET_DIR}/${name}"
    if [ ! -f "${path}" ]; then
        fail "Missing Steam library asset: ${name}"
        return
    fi

    local actual
    actual="$(ffprobe -v error -select_streams v:0 \
        -show_entries stream=width,height -of csv=s=x:p=0 "${path}")"
    if [ "${actual}" != "${expected}" ]; then
        fail "${name} is ${actual}; expected ${expected}"
    fi
}

check_dimensions library-capsule.png 600x900
check_dimensions library-hero.png 3840x1240
check_dimensions library-logo.png 1056x720
check_dimensions library-header.png 920x430
check_dimensions shortcut-icon.png 256x256

if [ -f "${ASSET_DIR}/library-logo.png" ]; then
    logo_pixel_format="$(ffprobe -v error -select_streams v:0 \
        -show_entries stream=pix_fmt -of default=nw=1:nk=1 \
        "${ASSET_DIR}/library-logo.png")"
    case "${logo_pixel_format}" in
        *a*) ;;
        *) fail "library-logo.png must retain transparency; found ${logo_pixel_format}" ;;
    esac
fi

if [ "${failures}" -ne 0 ]; then
    echo "Steam artwork validation failed with ${failures} error(s)." >&2
    exit 1
fi

echo "Steam library artwork validation passed: ${ASSET_DIR}"
