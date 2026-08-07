#!/usr/bin/env bash
# Rebuild the artwork used by Tater Tube's non-Steam library shortcut.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
SOURCE_DIR="${REPO_ROOT}/assets/steam/library/source"
OUTPUT_DIR="${STEAM_ASSET_OUTPUT_DIR:-${REPO_ROOT}/assets/steam/library/generated}"
LANDSCAPE="${SOURCE_DIR}/retro-media-room-landscape.png"
PORTRAIT="${SOURCE_DIR}/retro-media-room-portrait.png"
BRAND="${REPO_ROOT}/assets/images/tater-tube-readme.png"
MASCOT="${REPO_ROOT}/assets/images/mascots/game-center.png"

if ! command -v ffmpeg >/dev/null 2>&1; then
    echo "ffmpeg is required to build the Steam library artwork." >&2
    exit 1
fi

for source in "${LANDSCAPE}" "${PORTRAIT}" "${BRAND}" "${MASCOT}"; do
    if [ ! -f "${source}" ]; then
        echo "Missing artwork source: ${source}" >&2
        exit 1
    fi
done

mkdir -p "${OUTPUT_DIR}"

render_landscape() {
    local width="$1"
    local height="$2"
    local logo_width="$3"
    local logo_x="$4"
    local logo_y="$5"
    local mascot_width="$6"
    local mascot_x="$7"
    local mascot_y="$8"
    local output="$9"

    ffmpeg -hide_banner -loglevel error -y \
        -i "${LANDSCAPE}" -i "${BRAND}" -i "${MASCOT}" \
        -filter_complex \
        "[0:v]scale=${width}:${height}:force_original_aspect_ratio=increase:flags=lanczos,crop=${width}:${height},eq=brightness=-0.035:saturation=1.08[bg]; \
         [1:v]crop=862:588:290:198,scale=${logo_width}:-1:flags=lanczos,format=rgba[logo]; \
         [2:v]crop=940:1004:150:106,scale=${mascot_width}:-1:flags=lanczos,format=rgba[mascot]; \
         [bg][logo]overlay=${logo_x}:${logo_y}:format=auto[with_logo]; \
         [with_logo][mascot]overlay=${mascot_x}:${mascot_y}:format=auto,format=rgb24[out]" \
        -map "[out]" -frames:v 1 "${OUTPUT_DIR}/${output}"
}

render_portrait() {
    local width="$1"
    local height="$2"
    local logo_width="$3"
    local logo_x="$4"
    local logo_y="$5"
    local mascot_width="$6"
    local mascot_x="$7"
    local mascot_y="$8"
    local output="$9"

    ffmpeg -hide_banner -loglevel error -y \
        -i "${PORTRAIT}" -i "${BRAND}" -i "${MASCOT}" \
        -filter_complex \
        "[0:v]scale=${width}:${height}:force_original_aspect_ratio=increase:flags=lanczos,crop=${width}:${height},eq=brightness=-0.045:saturation=1.08[bg]; \
         [1:v]crop=862:588:290:198,scale=${logo_width}:-1:flags=lanczos,format=rgba[logo]; \
         [2:v]crop=940:1004:150:106,scale=${mascot_width}:-1:flags=lanczos,format=rgba[mascot]; \
         [bg][logo]overlay=${logo_x}:${logo_y}:format=auto[with_logo]; \
         [with_logo][mascot]overlay=${mascot_x}:${mascot_y}:format=auto,format=rgb24[out]" \
        -map "[out]" -frames:v 1 "${OUTPUT_DIR}/${output}"
}

render_portrait 600 900 450 75 55 430 85 390 library-capsule.png
render_landscape 920 430 430 35 68 300 585 85 library-header.png

# Steam's library hero must be artwork only: no product name or logo.
ffmpeg -hide_banner -loglevel error -y \
    -i "${LANDSCAPE}" \
    -vf "scale=3840:1240:force_original_aspect_ratio=increase:flags=lanczos,crop=3840:1240,eq=brightness=-0.035:saturation=1.08,format=rgb24" \
    -frames:v 1 "${OUTPUT_DIR}/library-hero.png"

# Steam overlays this transparent logo on the library hero.
ffmpeg -hide_banner -loglevel error -y \
    -i "${BRAND}" \
    -vf "crop=862:588:290:198,scale=-1:720:flags=lanczos,format=rgba" \
    -frames:v 1 "${OUTPUT_DIR}/library-logo.png"

# The non-Steam shortcut icon uses the exact existing Game Center mascot.
ffmpeg -hide_banner -loglevel error -y \
    -i "${PORTRAIT}" -i "${MASCOT}" \
    -filter_complex \
    "[0:v]scale=256:256:force_original_aspect_ratio=increase:flags=lanczos,crop=256:256,eq=brightness=-0.08:saturation=1.12[bg]; \
     [1:v]crop=940:1004:150:106,scale=215:-1:flags=lanczos,format=rgba[mascot]; \
     [bg][mascot]overlay=20:13:format=auto,format=rgba[out]" \
    -map "[out]" -frames:v 1 "${OUTPUT_DIR}/shortcut-icon.png"

"${SCRIPT_DIR}/validate-steam-library-artwork.sh" "${OUTPUT_DIR}"
