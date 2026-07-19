#!/usr/bin/env bash
# Validate the staged Steam depot without modifying it.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
DEPOT_ROOT="${1:-${STEAM_OUTPUT_DIR:-${REPO_ROOT}/out/steam}/depot}"
STRICT_RUNTIME="${STEAM_REQUIRE_COMPLETE_RUNTIME:-0}"
STRICT_PORTABLE="${STEAM_REQUIRE_PORTABLE_DEPOT:-${STRICT_RUNTIME}}"

failures=0

fail() {
    echo "ERROR: $*" >&2
    failures=$((failures + 1))
}

require_file() {
    if [ ! -f "${DEPOT_ROOT}/$1" ]; then
        fail "Missing depot file: $1"
    fi
}

require_executable() {
    if [ ! -x "${DEPOT_ROOT}/$1" ]; then
        fail "Missing depot executable: $1"
    fi
}

if [ ! -d "${DEPOT_ROOT}" ]; then
    echo "Depot does not exist: ${DEPOT_ROOT}" >&2
    exit 1
fi

require_executable "launch-tater-tube.sh"
require_executable "usr/bin/tater-tube"
require_file "usr/share/240mp/Main.qml"
require_file "SOURCE.txt"
require_file "LICENSE.txt"
require_file "BUILD-COMMIT.txt"

runtime_paths=(
    "usr/share/240mp/vendor/mpv/bin/mpv"
    "usr/share/240mp/vendor/moonlight-sdl/bin/moonlight"
    "usr/share/240mp/vendor/retroarch/bin/retroarch"
    "usr/share/240mp/vendor/yt-dlp/bin/yt-dlp"
    "usr/share/240mp/vendor/rclone/bin/rclone"
)

if [ "${STRICT_RUNTIME}" = "1" ]; then
    for runtime in "${runtime_paths[@]}"; do
        require_executable "${runtime}"
    done

    if ! find "${DEPOT_ROOT}/usr/share/240mp/vendor/retroarch/cores" \
            -maxdepth 1 -type f -name '*_libretro.so' -print -quit \
            2>/dev/null | grep -q .; then
        fail "No approved RetroArch cores are bundled"
    fi

    for notice in mpv moonlight-sdl retroarch retroarch-cores yt-dlp rclone qt; do
        require_file "THIRD_PARTY_NOTICES/${notice}.txt"
    done
    require_file "THIRD_PARTY_NOTICES/APPROVED-CORES.txt"

    approved_file="${DEPOT_ROOT}/THIRD_PARTY_NOTICES/APPROVED-CORES.txt"
    if [ -f "${approved_file}" ]; then
        while IFS= read -r core; do
            core_name="$(basename "${core}")"
            if ! grep -Fxq "${core_name}" "${approved_file}"; then
                fail "Bundled core is not listed in APPROVED-CORES.txt: ${core_name}"
            fi
        done < <(find "${DEPOT_ROOT}/usr/share/240mp/vendor/retroarch/cores" \
            -maxdepth 1 -type f -name '*_libretro.so' -print 2>/dev/null)
    fi
fi

if [ "${STRICT_PORTABLE}" = "1" ]; then
    if [ ! -d "${DEPOT_ROOT}/usr/plugins/platforms" ]; then
        fail "Qt platform plugins are not bundled under usr/plugins/platforms"
    fi
    if [ ! -d "${DEPOT_ROOT}/usr/qml/QtQuick" ]; then
        fail "Qt Quick modules are not bundled under usr/qml/QtQuick"
    fi
    if [ -n "${STEAM_QT_VERSION:-}" ]; then
        for component in qtbase qtdeclarative qtsvg; do
            require_file "THIRD_PARTY_NOTICES/qt-sbom/${component}-${STEAM_QT_VERSION}.spdx"
        done
    fi
fi

if command -v file >/dev/null 2>&1; then
    while IFS= read -r -d '' candidate; do
        description="$(file -b "${candidate}")"
        case "${description}" in
            *ELF*)
                case "${description}" in
                    *"x86-64"*) ;;
                    *) fail "Non-x86_64 ELF in depot: ${candidate#${DEPOT_ROOT}/}" ;;
                esac
                ;;
        esac
    done < <(find "${DEPOT_ROOT}" -type f \
        \( -perm -111 -o -name '*.so' -o -name '*.so.*' \) -print0)
else
    echo "WARNING: file is unavailable; ELF architecture checks were skipped." >&2
fi

if command -v ldd >/dev/null 2>&1; then
    library_path="${DEPOT_ROOT}/usr/lib:${DEPOT_ROOT}/usr/lib/tater-tube"
    while IFS= read -r -d '' candidate; do
        if ! file -b "${candidate}" 2>/dev/null | grep -q 'ELF.*dynamically linked'; then
            continue
        fi
        ldd_output="$(LD_LIBRARY_PATH="${library_path}" ldd "${candidate}" 2>&1 || true)"
        if grep -q 'not found' <<<"${ldd_output}"; then
            fail "Unresolved shared library for ${candidate#${DEPOT_ROOT}/}: $(grep 'not found' <<<"${ldd_output}" | tr '\n' ' ')"
        fi
    done < <(find "${DEPOT_ROOT}" -type f \
        \( -perm -111 -o -name '*.so' -o -name '*.so.*' \) -print0)
else
    echo "WARNING: ldd is unavailable; shared-library checks were skipped." >&2
fi

while IFS= read -r leaked_file; do
    fail "Private runtime data must not be shipped: ${leaked_file#${DEPOT_ROOT}/}"
done < <(find "${DEPOT_ROOT}" -type f \
    \( -name 'config.json' -o -name '*.credentials' \
       -o -name 'retronas-rclone.conf' \) -print)

if [ "${STEAM_ALLOW_CONTENT_FILES:-0}" != "1" ]; then
    while IFS= read -r content_file; do
        fail "Possible ROM, BIOS, or disc image in depot: ${content_file#${DEPOT_ROOT}/}"
    done < <(find "${DEPOT_ROOT}" -type f \
        \( -iname '*.nes' -o -iname '*.sfc' -o -iname '*.smc' \
           -o -iname '*.gba' -o -iname '*.gb' -o -iname '*.gbc' \
           -o -iname '*.a26' -o -iname '*.a52' -o -iname '*.a78' \
           -o -iname '*.cue' -o -iname '*.chd' -o -iname '*.iso' \
           -o -iname '*.rom' -o -iname '*.wad' -o -iname '*.m3u' \) -print)
fi

if [ "${failures}" -ne 0 ]; then
    echo "Steam depot validation failed with ${failures} error(s)." >&2
    exit 1
fi

echo "Steam depot validation passed: ${DEPOT_ROOT}"
