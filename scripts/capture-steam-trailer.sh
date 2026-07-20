#!/usr/bin/env bash
# Record a real 1920x1080 first-look trailer from the staged Linux depot.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
DEPOT_ROOT="${STEAM_DEPOT_ROOT:-${REPO_ROOT}/out/steam/depot}"
OUTPUT_DIR="${STEAM_TRAILER_OUTPUT_DIR:-${REPO_ROOT}/out/steam/trailer}"
OUTPUT_FILE="${OUTPUT_DIR}/tater-tube-first-look.mp4"
IMAGE_NAME="${STEAM_SNIPER_BUILDER_IMAGE:-tater-tube-steam-sniper-builder}"

for command in docker ffprobe; do
    if ! command -v "${command}" >/dev/null 2>&1; then
        echo "${command} is required to capture the Steam trailer." >&2
        exit 1
    fi
done

if [ ! -x "${DEPOT_ROOT}/launch-tater-tube.sh" ]; then
    echo "Build the Steam depot before capturing a trailer: ${DEPOT_ROOT}" >&2
    exit 1
fi

mkdir -p "${OUTPUT_DIR}"

docker run --rm --platform linux/amd64 \
    --entrypoint /bin/bash \
    -v "${REPO_ROOT}:/src" \
    -v "${DEPOT_ROOT}:/depot:ro" \
    -v "${OUTPUT_DIR}:/trailer" \
    "${IMAGE_NAME}" -lc '
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get update >/dev/null
apt-get install -y --no-install-recommends ffmpeg x11-apps xdotool >/dev/null
rm -rf /var/lib/apt/lists/*

export DISPLAY=:99
export HOME=/tmp/tater-tube-trailer-home
mkdir -p "${HOME}"

Xvfb "${DISPLAY}" -screen 0 1920x1080x24 -ac +extension GLX \
    >/tmp/tater-tube-xvfb.log 2>&1 &
xvfb_pid=$!
app_pid=
ffmpeg_pid=

cleanup() {
    if [ -n "${ffmpeg_pid}" ]; then
        kill "${ffmpeg_pid}" 2>/dev/null || true
    fi
    if [ -n "${app_pid}" ]; then
        kill "${app_pid}" 2>/dev/null || true
    fi
    kill "${xvfb_pid}" 2>/dev/null || true
}
trap cleanup EXIT

sleep 1
/depot/launch-tater-tube.sh \
    >/tmp/tater-tube-trailer-app.log 2>&1 &
app_pid=$!

for _ in $(seq 1 30); do
    if xdotool search --onlyvisible --class tater-tube >/dev/null 2>&1 \
            || xdotool search --onlyvisible --name "Tater Tube" >/dev/null 2>&1; then
        break
    fi
    if ! kill -0 "${app_pid}" 2>/dev/null; then
        cat /tmp/tater-tube-trailer-app.log >&2
        exit 1
    fi
    sleep 0.5
done

sleep 2
ffmpeg -hide_banner -loglevel error -y \
    -f x11grab -framerate 30 -video_size 1920x1080 -i "${DISPLAY}" \
    -f lavfi -i anullsrc=channel_layout=stereo:sample_rate=48000 \
    -t 30 \
    -c:v libx264 -preset medium -b:v 8000k -maxrate 10000k -bufsize 16000k \
    -pix_fmt yuv420p -r 30 \
    -c:a aac -b:a 192k -ar 48000 \
    -movflags +faststart -shortest \
    /trailer/tater-tube-first-look.mp4 &
ffmpeg_pid=$!

sleep 3
for _ in $(seq 1 5); do
    xdotool key --clearmodifiers Down
    sleep 0.35
done
sleep 2
xdotool key --clearmodifiers Return
sleep 5
xdotool key --clearmodifiers Escape
sleep 1
xdotool key --clearmodifiers Down
sleep 0.35
xdotool key --clearmodifiers Down
sleep 0.35
xdotool key --clearmodifiers Return
sleep 5
xdotool key --clearmodifiers Escape
sleep 1
xdotool key --clearmodifiers Escape
sleep 3
xdotool key --clearmodifiers Down
sleep 0.5
xdotool key --clearmodifiers Down
sleep 0.5
xdotool key --clearmodifiers Down

wait "${ffmpeg_pid}"
ffmpeg_pid=
'

video_dimensions="$(ffprobe -v error -select_streams v:0 \
    -show_entries stream=width,height -of csv=s=x:p=0 "${OUTPUT_FILE}")"
video_rate="$(ffprobe -v error -select_streams v:0 \
    -show_entries stream=avg_frame_rate -of default=nw=1:nk=1 "${OUTPUT_FILE}")"
video_codec="$(ffprobe -v error -select_streams v:0 \
    -show_entries stream=codec_name -of default=nw=1:nk=1 "${OUTPUT_FILE}")"
audio_codec="$(ffprobe -v error -select_streams a:0 \
    -show_entries stream=codec_name -of default=nw=1:nk=1 "${OUTPUT_FILE}")"

if [ "${video_dimensions}" != "1920x1080" ] \
        || [ "${video_rate}" != "30/1" ] \
        || [ "${video_codec}" != "h264" ] \
        || [ "${audio_codec}" != "aac" ]; then
    echo "Trailer validation failed: ${video_dimensions}, ${video_rate}, ${video_codec}/${audio_codec}" >&2
    exit 1
fi

echo "Real application trailer captured at ${OUTPUT_FILE}"
