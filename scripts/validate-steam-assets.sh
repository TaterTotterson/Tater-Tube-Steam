#!/usr/bin/env bash
# Validate dimensions and formats for required Steam graphical assets.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
ASSET_DIR="${1:-${REPO_ROOT}/assets/steam/store/generated}"
failures=0

if ! command -v ffprobe >/dev/null 2>&1; then
    echo "ffprobe is required to validate the Steam artwork." >&2
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
        fail "Missing Steam asset: ${name}"
        return
    fi

    local actual
    actual="$(ffprobe -v error -select_streams v:0 \
        -show_entries stream=width,height -of csv=s=x:p=0 "${path}")"
    if [ "${actual}" != "${expected}" ]; then
        fail "${name} is ${actual}; expected ${expected}"
    fi
}

check_dimensions header-capsule.png 920x430
check_dimensions small-capsule.png 462x174
check_dimensions main-capsule.png 1232x706
check_dimensions vertical-capsule.png 748x896
check_dimensions library-capsule.png 600x900
check_dimensions library-hero.png 3840x1240
check_dimensions library-logo.png 1000x682
check_dimensions library-header.png 920x430
check_dimensions shortcut-icon.png 256x256
check_dimensions app-icon.jpg 184x184

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

echo "Steam artwork validation passed: ${ASSET_DIR}"
