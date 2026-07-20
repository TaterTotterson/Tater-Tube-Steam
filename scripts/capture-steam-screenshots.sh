#!/usr/bin/env bash
# Capture real 1920x1080 product screenshots from the staged Linux depot.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
DEPOT_ROOT="${STEAM_DEPOT_ROOT:-${REPO_ROOT}/out/steam/depot}"
OUTPUT_DIR="${STEAM_SCREENSHOT_OUTPUT_DIR:-${REPO_ROOT}/assets/steam/store/screenshots}"
IMAGE_NAME="${STEAM_SNIPER_BUILDER_IMAGE:-tater-tube-steam-sniper-builder}"

for command in docker ffmpeg ffprobe; do
    if ! command -v "${command}" >/dev/null 2>&1; then
        echo "${command} is required to capture Steam screenshots." >&2
        exit 1
    fi
done

if [ ! -x "${DEPOT_ROOT}/launch-tater-tube.sh" ]; then
    echo "Build the Steam depot before capturing screenshots: ${DEPOT_ROOT}" >&2
    exit 1
fi

mkdir -p "${OUTPUT_DIR}"
rm -f \
    "${OUTPUT_DIR}/01-home.xwd" \
    "${OUTPUT_DIR}/02-game-center-selected.xwd" \
    "${OUTPUT_DIR}/03-retronas-setup.xwd" \
    "${OUTPUT_DIR}/04-pc-link.xwd" \
    "${OUTPUT_DIR}/05-settings.xwd" \
    "${OUTPUT_DIR}/01-home.png" \
    "${OUTPUT_DIR}/02-game-center-selected.png" \
    "${OUTPUT_DIR}/03-retronas-setup.png" \
    "${OUTPUT_DIR}/04-pc-link.png" \
    "${OUTPUT_DIR}/04-settings.png" \
    "${OUTPUT_DIR}/05-settings.png"

docker run --rm --platform linux/amd64 \
    --entrypoint /bin/bash \
    -v "${REPO_ROOT}:/src" \
    -v "${DEPOT_ROOT}:/depot:ro" \
    -v "${OUTPUT_DIR}:/screenshots" \
    "${IMAGE_NAME}" -lc '
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get update >/dev/null
apt-get install -y --no-install-recommends x11-apps xdotool >/dev/null
rm -rf /var/lib/apt/lists/*

export DISPLAY=:99
export HOME=/tmp/tater-tube-screenshot-home
export DATA_ROOT=/tmp/tater-tube-screenshot-data
mkdir -p "${HOME}" "${DATA_ROOT}"

Xvfb "${DISPLAY}" -screen 0 1920x1080x24 -ac +extension GLX \
    >/tmp/tater-tube-xvfb.log 2>&1 &
xvfb_pid=$!
app_pid=

cleanup() {
    if [ -n "${app_pid}" ]; then
        kill "${app_pid}" 2>/dev/null || true
    fi
    kill "${xvfb_pid}" 2>/dev/null || true
}
trap cleanup EXIT

sleep 1
/depot/launch-tater-tube.sh \
    >/tmp/tater-tube-screenshot-app.log 2>&1 &
app_pid=$!

for _ in $(seq 1 30); do
    if xdotool search --onlyvisible --class tater-tube >/dev/null 2>&1 \
            || xdotool search --onlyvisible --name "Tater Tube" >/dev/null 2>&1; then
        break
    fi
    if ! kill -0 "${app_pid}" 2>/dev/null; then
        cat /tmp/tater-tube-screenshot-app.log >&2
        exit 1
    fi
    sleep 0.5
done

sleep 2
xwd -root -silent -out /screenshots/01-home.xwd

for _ in $(seq 1 5); do
    xdotool key --clearmodifiers Down
    sleep 0.15
done
xwd -root -silent -out /screenshots/02-game-center-selected.xwd

xdotool key --clearmodifiers Return
sleep 2
xwd -root -silent -out /screenshots/03-retronas-setup.xwd

xdotool key --clearmodifiers Escape
sleep 1
xdotool key --clearmodifiers Down
xdotool key --clearmodifiers Down
xdotool key --clearmodifiers Return
sleep 2
xwd -root -silent -out /screenshots/04-pc-link.xwd

xdotool key --clearmodifiers Escape
sleep 1
xdotool key --clearmodifiers Escape
sleep 1
xwd -root -silent -out /screenshots/05-settings.xwd
'

for source in "${OUTPUT_DIR}"/*.xwd; do
    destination="${source%.xwd}.png"
    ffmpeg -hide_banner -loglevel error -y \
        -i "${source}" -vf "format=rgb24" -frames:v 1 "${destination}"
    rm -f "${source}"
done

for screenshot in "${OUTPUT_DIR}"/*.png; do
    dimensions="$(ffprobe -v error -select_streams v:0 \
        -show_entries stream=width,height -of csv=s=x:p=0 "${screenshot}")"
    if [ "${dimensions}" != "1920x1080" ]; then
        echo "Screenshot has invalid dimensions: ${screenshot} (${dimensions})" >&2
        exit 1
    fi
done

echo "Real application screenshots captured in ${OUTPUT_DIR}"
