#!/usr/bin/env bash
# Build a Linux x86_64 Steam depot. Run on Linux or in the Steam Linux Runtime SDK.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${STEAM_BUILD_DIR:-${REPO_ROOT}/build-steam-linux}"
OUTPUT_ROOT="${STEAM_OUTPUT_DIR:-${REPO_ROOT}/out/steam}"
DEPOT_ROOT="${OUTPUT_ROOT}/depot"

require_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Missing required command: $1" >&2
        exit 1
    fi
}

if [ "$(uname -s)" != "Linux" ]; then
    echo "The Steam depot must be built on Linux. Use an x86_64 Linux machine or SDK container." >&2
    exit 1
fi

case "$(uname -m)" in
    x86_64|amd64) ;;
    *)
        echo "The first Steam depot targets Linux x86_64; found $(uname -m)." >&2
        exit 1
        ;;
esac

require_cmd cmake

source_commit="${STEAM_SOURCE_COMMIT:-}"
source_state="${STEAM_SOURCE_STATE:-}"
if git -C "${REPO_ROOT}" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    source_commit="$(git -C "${REPO_ROOT}" rev-parse HEAD)"
    if git -C "${REPO_ROOT}" diff --quiet \
            && git -C "${REPO_ROOT}" diff --cached --quiet; then
        source_state="clean"
    else
        source_state="dirty-development-build"
    fi
fi
source_commit="${source_commit:-unknown}"
source_state="${source_state:-unknown}"

if [ "${STEAM_RELEASE:-0}" = "1" ] && [ "${source_state}" != "clean" ]; then
    echo "Refusing a release depot from a dirty worktree." >&2
    exit 1
fi

if [ "${STEAM_REQUIRE_PORTABLE_DEPOT:-${STEAM_REQUIRE_COMPLETE_RUNTIME:-0}}" = "1" ] \
        && [ -z "${LINUXDEPLOY:-}" ]; then
    echo "A portable release depot requires LINUXDEPLOY." >&2
    exit 1
fi

stage_bundle() {
    local source_dir="$1"
    local destination_dir="$2"
    local label="$3"

    if [ -z "${source_dir}" ]; then
        return
    fi
    if [ ! -d "${source_dir}" ]; then
        echo "${label} bundle does not exist: ${source_dir}" >&2
        exit 1
    fi

    cmake -E remove_directory "${destination_dir}"
    cmake -E make_directory "${destination_dir}"
    cmake -E copy_directory "${source_dir}" "${destination_dir}"
}

cmake -E remove_directory "${DEPOT_ROOT}"
cmake -E make_directory "${DEPOT_ROOT}"

generator_args=()
if command -v ninja >/dev/null 2>&1; then
    generator_args=(-G Ninja)
fi

cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" \
    "${generator_args[@]}" \
    -DTATER_TUBE_STEAM=ON \
    -DBUILD_TESTING="${STEAM_BUILD_TESTS:-ON}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr

cmake --build "${BUILD_DIR}" --parallel
if [ "${STEAM_BUILD_TESTS:-ON}" = "ON" ]; then
    ctest --test-dir "${BUILD_DIR}" --output-on-failure
fi
DESTDIR="${DEPOT_ROOT}" cmake --install "${BUILD_DIR}"

cmake -E copy \
    "${REPO_ROOT}/packaging/steam/launch-tater-tube.sh" \
    "${DEPOT_ROOT}/launch-tater-tube.sh"
cmake -E copy \
    "${REPO_ROOT}/packaging/steam/SOURCE.txt" \
    "${DEPOT_ROOT}/SOURCE.txt"
cmake -E copy \
    "${REPO_ROOT}/LICENSE" \
    "${DEPOT_ROOT}/LICENSE.txt"
chmod +x "${DEPOT_ROOT}/launch-tater-tube.sh"
echo "${source_commit}" > "${DEPOT_ROOT}/BUILD-COMMIT.txt"
echo "${source_state}" > "${DEPOT_ROOT}/BUILD-STATE.txt"

VENDOR_ROOT="${DEPOT_ROOT}/usr/share/240mp/vendor"
stage_bundle "${STEAM_MPV_BUNDLE:-}" "${VENDOR_ROOT}/mpv" "mpv"
stage_bundle "${STEAM_MOONLIGHT_BUNDLE:-}" "${VENDOR_ROOT}/moonlight-sdl" "Moonlight"
stage_bundle "${STEAM_RETROARCH_BUNDLE:-}" "${VENDOR_ROOT}/retroarch" "RetroArch"
stage_bundle "${STEAM_YTDLP_BUNDLE:-}" "${VENDOR_ROOT}/yt-dlp" "yt-dlp"
stage_bundle "${STEAM_RCLONE_BUNDLE:-}" "${VENDOR_ROOT}/rclone" "rclone"

if [ -n "${STEAM_THIRD_PARTY_NOTICES_DIR:-}" ]; then
    stage_bundle "${STEAM_THIRD_PARTY_NOTICES_DIR}" \
        "${DEPOT_ROOT}/THIRD_PARTY_NOTICES" "third-party notices"
fi

if [ -n "${LINUXDEPLOY:-}" ]; then
    if [ ! -x "${LINUXDEPLOY}" ]; then
        echo "LINUXDEPLOY is set but is not executable: ${LINUXDEPLOY}" >&2
        exit 1
    fi
    linuxdeploy_args=(
        --appdir "${DEPOT_ROOT}"
        --executable "${DEPOT_ROOT}/usr/bin/tater-tube"
        --desktop-file "${REPO_ROOT}/packaging/steam/tater-tube.desktop"
        --icon-file "${REPO_ROOT}/assets/images/logo.svg"
        --plugin qt
    )
    for runtime in \
        "${VENDOR_ROOT}/mpv/bin/mpv" \
        "${VENDOR_ROOT}/moonlight-sdl/bin/moonlight" \
        "${VENDOR_ROOT}/retroarch/bin/retroarch"; do
        if [ -x "${runtime}" ]; then
            linuxdeploy_args+=(--executable "${runtime}")
        fi
    done
    "${LINUXDEPLOY}" "${linuxdeploy_args[@]}"

    # The launcher selects Qt Quick Controls' Basic style explicitly, so the
    # other desktop style packs and Qt Creator design metadata are unnecessary.
    for unused_style in \
        FluentWinUI3 Fusion Imagine Material NativeStyle Universal designer; do
        cmake -E remove_directory \
            "${DEPOT_ROOT}/usr/qml/QtQuick/Controls/${unused_style}"
    done
else
    echo "LINUXDEPLOY is not set; the depot contains the app but not bundled Qt/system libraries."
    echo "Set LINUXDEPLOY to a linuxdeploy executable with its Qt plugin available for a portable depot."
fi

if [ -n "${STEAM_QT_SBOM_DIR:-}" ]; then
    if [ ! -d "${STEAM_QT_SBOM_DIR}" ]; then
        echo "Qt SBOM directory does not exist: ${STEAM_QT_SBOM_DIR}" >&2
        exit 1
    fi
    qt_notices="${DEPOT_ROOT}/THIRD_PARTY_NOTICES"
    cmake -E make_directory "${qt_notices}/qt-sbom"
    for component in qtbase qtdeclarative qtsvg; do
        sbom="${STEAM_QT_SBOM_DIR}/${component}-${STEAM_QT_VERSION:-unknown}.spdx"
        if [ ! -f "${sbom}" ]; then
            echo "Missing Qt SPDX document: ${sbom}" >&2
            exit 1
        fi
        cmake -E copy "${sbom}" "${qt_notices}/qt-sbom/"
    done
    {
        echo "Qt ${STEAM_QT_VERSION:-unknown}"
        echo "Runtime components: Qt Base, Qt Declarative, Qt SVG"
        echo "Dynamically linked; distributed under GPL-3.0-only with Tater Tube."
        echo "Source: https://code.qt.io/cgit/qt/qt5.git/tag/?h=v${STEAM_QT_VERSION:-unknown}"
        echo "Component SPDX documents are included under qt-sbom/."
    } > "${qt_notices}/qt.txt"
fi

missing=0
for runtime in \
    "usr/share/240mp/vendor/mpv/bin/mpv" \
    "usr/share/240mp/vendor/moonlight-sdl/bin/moonlight" \
    "usr/share/240mp/vendor/retroarch/bin/retroarch" \
    "usr/share/240mp/vendor/yt-dlp/bin/yt-dlp" \
    "usr/share/240mp/vendor/rclone/bin/rclone"; do
    if [ ! -x "${DEPOT_ROOT}/${runtime}" ]; then
        echo "Runtime not bundled yet: ${runtime}"
        missing=1
    fi
done

if ! find "${VENDOR_ROOT}/retroarch/cores" -maxdepth 1 -type f \
        -name '*_libretro.so' -print -quit 2>/dev/null | grep -q .; then
    echo "Runtime not bundled yet: usr/share/240mp/vendor/retroarch/cores/*_libretro.so"
    missing=1
fi

if [ "${STEAM_REQUIRE_COMPLETE_RUNTIME:-0}" = "1" ] && [ "${missing}" -ne 0 ]; then
    echo "Refusing an incomplete depot because STEAM_REQUIRE_COMPLETE_RUNTIME=1." >&2
    exit 1
fi

if command -v sha256sum >/dev/null 2>&1; then
    (
        cd "${DEPOT_ROOT}"
        find . -type f ! -name 'DEPOT-SHA256SUMS.txt' -print0 \
            | sort -z \
            | xargs -0 sha256sum
    ) > "${DEPOT_ROOT}/DEPOT-SHA256SUMS.txt"
elif command -v shasum >/dev/null 2>&1; then
    (
        cd "${DEPOT_ROOT}"
        find . -type f ! -name 'DEPOT-SHA256SUMS.txt' -print0 \
            | sort -z \
            | xargs -0 shasum -a 256
    ) > "${DEPOT_ROOT}/DEPOT-SHA256SUMS.txt"
fi

"${REPO_ROOT}/scripts/validate-steam-depot.sh" "${DEPOT_ROOT}"

echo "Steam depot staged at ${DEPOT_ROOT}"
echo "Steam launch executable: ./launch-tater-tube.sh"
