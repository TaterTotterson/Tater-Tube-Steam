#!/usr/bin/env bash
# Package locally built Steam runtimes into a reusable, checksum-addressed archive.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
INPUT_DIR="${1:-${REPO_ROOT}/out/steam-runtime}"
OUTPUT_FILE="${2:-${REPO_ROOT}/out/runtime-bundle/Tater-Tube-Steam-runtime-linux-x86_64.tar.zst}"

required_files=(
    mpv/bin/mpv
    moonlight-sdl/bin/moonlight
    retroarch/bin/retroarch
    ports/sm64coopdx/sm64coopdx
    ports/2ship2harkinian/2s2h.elf
    ports/shipwright/soh.elf
    ports/spaghettikart/Spaghettify
    ports/starship/Starship
    ports/dusklight/dusklight-bin
    yt-dlp/bin/yt-dlp
    rclone/bin/rclone
    notices/APPROVED-CORES.txt
)

for relative_path in "${required_files[@]}"; do
    if [ ! -f "${INPUT_DIR}/${relative_path}" ]; then
        echo "Runtime bundle input is incomplete: ${relative_path}" >&2
        exit 1
    fi
done

if find "${INPUT_DIR}" -type f \
        \( -iname '*.z64' -o -iname '*.n64' -o -iname '*.v64' \
           -o -iname '*.iso' -o -iname '*.chd' -o -iname '*.cue' \
           -o -iname 'mm.o2r' -o -iname 'oot.o2r' -o -iname 'oot.otr' \) \
        -print -quit | grep -q .; then
    echo "Refusing to package a runtime bundle containing game data." >&2
    exit 1
fi

mkdir -p "$(dirname "${OUTPUT_FILE}")"
rm -f "${OUTPUT_FILE}" "${OUTPUT_FILE}.sha256"

if command -v gtar >/dev/null 2>&1; then
    source_date_epoch="$(git -C "${REPO_ROOT}" show -s --format=%ct HEAD)"
    gtar \
        --sort=name \
        --mtime="@${source_date_epoch}" \
        --owner=0 \
        --group=0 \
        --numeric-owner \
        -I 'zstd -10 -T0' \
        -cf "${OUTPUT_FILE}" \
        -C "${INPUT_DIR}" .
else
    COPYFILE_DISABLE=1 tar --zstd -cf "${OUTPUT_FILE}" -C "${INPUT_DIR}" .
fi

zstd --test "${OUTPUT_FILE}"
tar --zstd -tf "${OUTPUT_FILE}" >/dev/null
if command -v sha256sum >/dev/null 2>&1; then
    (
        cd "$(dirname "${OUTPUT_FILE}")"
        sha256sum "$(basename "${OUTPUT_FILE}")" \
            > "$(basename "${OUTPUT_FILE}").sha256"
    )
else
    (
        cd "$(dirname "${OUTPUT_FILE}")"
        shasum -a 256 "$(basename "${OUTPUT_FILE}")" \
            > "$(basename "${OUTPUT_FILE}").sha256"
    )
fi

ls -lh "${OUTPUT_FILE}" "${OUTPUT_FILE}.sha256"
