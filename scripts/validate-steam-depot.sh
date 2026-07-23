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
require_file "usr/share/240mp/views/TaterPicks.qml"
require_file "usr/share/240mp/modules/shared/TaterBumpers.js"
require_file "usr/share/240mp/scripts/playback-transition.lua"
require_file "usr/share/240mp/assets/images/mascots/tater-picks.png"
require_file "SOURCE.txt"
require_file "LICENSE.txt"
require_file "BUILD-COMMIT.txt"

bumper_count="$(
    find "${DEPOT_ROOT}/usr/share/240mp/assets/videos/tater-bumpers" \
        -maxdepth 1 -type f -name '*.mp4' 2>/dev/null | wc -l | tr -d ' '
)"
if [ "${bumper_count:-0}" -ne 16 ]; then
    fail "Expected 16 Tater bumper videos, found ${bumper_count:-0}"
fi

runtime_paths=(
    "usr/share/240mp/vendor/mpv/bin/mpv"
    "usr/share/240mp/vendor/moonlight-sdl/bin/moonlight"
    "usr/share/240mp/vendor/retroarch/bin/retroarch"
    "usr/share/240mp/vendor/yt-dlp/bin/yt-dlp"
    "usr/share/240mp/vendor/rclone/bin/rclone"
    "usr/share/240mp/vendor/ports/sm64coopdx/sm64coopdx"
    "usr/share/240mp/vendor/ports/2ship2harkinian/2ship2harkinian"
    "usr/share/240mp/vendor/ports/2ship2harkinian/2s2h.elf"
    "usr/share/240mp/vendor/ports/shipwright/shipwright"
    "usr/share/240mp/vendor/ports/shipwright/soh.elf"
    "usr/share/240mp/vendor/ports/spaghettikart/spaghettikart"
    "usr/share/240mp/vendor/ports/spaghettikart/Spaghettify"
    "usr/share/240mp/vendor/ports/starship/starship"
    "usr/share/240mp/vendor/ports/starship/starship-bin"
    "usr/share/240mp/vendor/ports/dusklight/dusklight"
    "usr/share/240mp/vendor/ports/dusklight/dusklight-bin"
)

mpv_runtime="${DEPOT_ROOT}/usr/share/240mp/vendor/mpv/bin/mpv"
if [ -x "${mpv_runtime}" ]; then
    mpv_options="$(
        LD_LIBRARY_PATH="${DEPOT_ROOT}/usr/lib:${DEPOT_ROOT}/usr/lib/tater-tube:${DEPOT_ROOT}/usr/share/240mp/vendor/mpv/lib" \
            "${mpv_runtime}" --list-options 2>&1
    )" || fail "Bundled mpv could not report its available options"
    if ! grep -Eq '^ --osc[[:space:]]' <<<"${mpv_options}"; then
        fail "Bundled mpv lacks Lua OSC support"
    fi
    if ! grep -Eq '^ --ytdl[[:space:]]' <<<"${mpv_options}"; then
        fail "Bundled mpv lacks the yt-dlp hook"
    fi
fi

if [ "${STRICT_RUNTIME}" = "1" ]; then
    for runtime in "${runtime_paths[@]}"; do
        require_executable "${runtime}"
    done
    require_file "usr/share/240mp/vendor/ports/sm64coopdx/SOURCE.txt"
    require_file "usr/share/240mp/vendor/ports/2ship2harkinian/SOURCE.txt"
    require_file "usr/share/240mp/vendor/ports/2ship2harkinian/LICENSE.txt"
    require_file "usr/share/240mp/vendor/ports/2ship2harkinian/2ship.o2r"
    require_file "usr/share/240mp/vendor/ports/2ship2harkinian/assets/Config_N64_US.xml"
    require_file "usr/share/240mp/vendor/ports/2ship2harkinian/assets/Config_GC_US.xml"
    require_file "usr/share/240mp/vendor/ports/shipwright/SOURCE.txt"
    require_file "usr/share/240mp/vendor/ports/shipwright/soh.o2r"
    require_file "usr/share/240mp/vendor/ports/shipwright/LICENSES/libultraship.txt"
    require_file "usr/share/240mp/vendor/ports/shipwright/LICENSES/OTRExporter.txt"
    require_file "usr/share/240mp/vendor/ports/shipwright/LICENSES/ZAPDTR.txt"
    require_file "usr/share/240mp/vendor/ports/shipwright/assets/Config_N64_NTSC_10.xml"
    require_file "usr/share/240mp/vendor/ports/shipwright/assets/Config_GC_MQ_NTSC_U.xml"
    require_file "usr/share/240mp/vendor/ports/spaghettikart/SOURCE.txt"
    require_file "usr/share/240mp/vendor/ports/spaghettikart/spaghetti.o2r"
    require_file "usr/share/240mp/vendor/ports/spaghettikart/config.yml"
    require_file "usr/share/240mp/vendor/ports/spaghettikart/meta/mods.toml"
    require_file "usr/share/240mp/vendor/ports/spaghettikart/LICENSES/libultraship.txt"
    require_file "usr/share/240mp/vendor/ports/spaghettikart/LICENSES/torch.txt"
    require_file "usr/share/240mp/vendor/ports/spaghettikart/LICENSES/StormLib.txt"
    require_file "usr/share/240mp/vendor/ports/starship/SOURCE.txt"
    require_file "usr/share/240mp/vendor/ports/starship/starship.o2r"
    require_file "usr/share/240mp/vendor/ports/starship/config.yml"
    require_file "usr/share/240mp/vendor/ports/starship/assets/yaml/us/rev0/ast_audio.yaml"
    require_file "usr/share/240mp/vendor/ports/starship/assets/yaml/us/rev1/ast_audio.yaml"
    require_file "usr/share/240mp/vendor/ports/starship/LICENSES/Starship.txt"
    require_file "usr/share/240mp/vendor/ports/starship/LICENSES/libultraship.txt"
    require_file "usr/share/240mp/vendor/ports/starship/LICENSES/Torch.txt"
    require_file "usr/share/240mp/vendor/ports/dusklight/SOURCE.txt"
    require_file "usr/share/240mp/vendor/ports/dusklight/res/logo.png"
    require_file "usr/share/240mp/vendor/ports/dusklight/res/rml/prelaunch.rcss"
    require_file "usr/share/240mp/vendor/ports/dusklight/LICENSES/Dusklight-CC0-1.0.txt"
    require_file "usr/share/240mp/vendor/ports/dusklight/LICENSES/Aurora-MIT.txt"

    if ! find "${DEPOT_ROOT}/usr/share/240mp/vendor/retroarch/cores" \
            -maxdepth 1 -type f -name '*_libretro.so' -print -quit \
            2>/dev/null | grep -q .; then
        fail "No approved RetroArch cores are bundled"
    fi

    for notice in mpv moonlight-sdl retroarch retroarch-cores yt-dlp rclone qt; do
        require_file "THIRD_PARTY_NOTICES/${notice}.txt"
    done
    require_file "THIRD_PARTY_NOTICES/APPROVED-CORES.txt"
    require_file "THIRD_PARTY_NOTICES/retroarch-cores/CORE-MANIFEST.tsv"
    require_file "THIRD_PARTY_NOTICES/retroarch-cores/CORE-SHA256SUMS.txt"
    require_file "THIRD_PARTY_NOTICES/retroarch-cores/SOURCE-SHA256SUMS.txt"
    require_file "THIRD_PARTY_NOTICES/retroarch-cores/SUPPORT-SHA256SUMS.txt"
    require_file "usr/share/240mp/vendor/retroarch/system/bluemsx/Databases/msxromdb.xml"
    require_file "usr/share/240mp/vendor/retroarch/system/bluemsx/Machines/MSX - C-BIOS/cbios.txt"
    require_file "usr/share/240mp/vendor/retroarch/system/bluemsx/Machines/MSX - C-BIOS/cbios_logo_msx1.rom"
    require_file "usr/share/240mp/vendor/retroarch/system/bluemsx/Machines/MSX - C-BIOS/cbios_main_msx1.rom"

    approved_file="${DEPOT_ROOT}/THIRD_PARTY_NOTICES/APPROVED-CORES.txt"
    if [ -f "${approved_file}" ]; then
        while IFS= read -r core; do
            core_name="$(basename "${core}")"
            if ! grep -Fxq "${core_name}" "${approved_file}"; then
                fail "Bundled core is not listed in APPROVED-CORES.txt: ${core_name}"
            fi
        done < <(find "${DEPOT_ROOT}/usr/share/240mp/vendor/retroarch/cores" \
            -maxdepth 1 -type f -name '*_libretro.so' -print 2>/dev/null)

        while IFS= read -r core_name; do
            [ -n "${core_name}" ] || continue
            require_file "usr/share/240mp/vendor/retroarch/cores/${core_name}"
        done < "${approved_file}"

        approved_count="$(grep -Ec '^[A-Za-z0-9_.+-]+_libretro\.so$' \
            "${approved_file}")"
        source_count="$(
            find "${DEPOT_ROOT}/THIRD_PARTY_NOTICES/retroarch-cores/sources" \
                -maxdepth 1 -type f -name '*.tar.gz' 2>/dev/null \
                | wc -l | tr -d ' '
        )"
        if [ "${source_count:-0}" -ne "${approved_count:-0}" ]; then
            fail "Expected ${approved_count:-0} corresponding core source archives, found ${source_count:-0}"
        fi

        manifest_file="${DEPOT_ROOT}/THIRD_PARTY_NOTICES/retroarch-cores/CORE-MANIFEST.tsv"
        if [ -f "${manifest_file}" ]; then
            invalid_manifest_rows="$(
                awk -F'|' '
                    !/^#/ && NF {
                        invalid = 0
                        if (NF != 9) invalid = 1
                        if ($1 !~ /^(standard|replacement|arcade)$/) invalid = 1
                        if ($3 !~ /^[A-Za-z0-9_.+-]+_libretro[.]so$/) invalid = 1
                        if (length($5) != 40) invalid = 1
                        if ($5 !~ /^[0-9a-f]+$/) invalid = 1
                        if (invalid) {
                            print NR
                        }
                    }
                ' "${manifest_file}"
            )"
            if [ -n "${invalid_manifest_rows}" ]; then
                fail "Invalid core manifest row(s): $(tr '\n' ' ' <<<"${invalid_manifest_rows}")"
            fi

            manifest_core_names="$(
                awk -F'|' '!/^#/ && NF { print $3 }' "${manifest_file}" \
                    | sort -u
            )"
            approved_core_names="$(sort -u "${approved_file}")"
            if [ "${manifest_core_names}" != "${approved_core_names}" ]; then
                fail "CORE-MANIFEST.tsv filenames do not match APPROVED-CORES.txt"
            fi

            while IFS='|' read -r group core_id _core_file _repository revision \
                    _license _build_subdir _makefile _make_arguments; do
                case "${group}" in
                    ''|\#*) continue ;;
                esac
                require_file "THIRD_PARTY_NOTICES/retroarch-cores/sources/${core_id}-${revision}.tar.gz"
                license_dir="${DEPOT_ROOT}/THIRD_PARTY_NOTICES/retroarch-cores/licenses/${core_id}"
                if ! find "${license_dir}" -type f -print -quit 2>/dev/null \
                        | grep -q .; then
                    fail "Missing upstream license files for core: ${core_id}"
                fi
            done < "${manifest_file}"
        fi

        if command -v tar >/dev/null 2>&1; then
            while IFS= read -r source_archive; do
                prohibited_source_entries="$(
                    tar -tzf "${source_archive}" 2>/dev/null \
                        | grep -Ei '\.(7z|32x|a|a26|a52|a78|atr|atx|bin|car|cas|cbn|ccd|cdf|chd|col|com|cue|dcm|dmg|dsk|dylib|exe|fds|fig|gb|gba|gbc|gen|gg|img|int|ipk3|iso|iwad|lmp|lnx|m3u|md|mdf|min|mvc|mx1|mx2|neo|nes|ngc|ngp|ngpc|npc|o|o2|pak|pbp|pc2|pce|pk3|pwad|ri|rom|sc|sf|sfc|sg|sgb|sgx|smc|smd|sms|so|toc|unf|unif|vb|vec|wad|ws|wsc|xfd|xex|zip)$' \
                        | sed -n '1,20p' || true
                )"
                if [ -n "${prohibited_source_entries}" ]; then
                    fail "Possible ROM, firmware, game data, or build output in corresponding-source archive $(basename "${source_archive}"): $(tr '\n' ' ' <<<"${prohibited_source_entries}")"
                fi
            done < <(
                find "${DEPOT_ROOT}/THIRD_PARTY_NOTICES/retroarch-cores/sources" \
                    -maxdepth 1 -type f -name '*.tar.gz' -print
            )
        else
            echo "WARNING: tar is unavailable; core-source content checks were skipped." >&2
        fi

        if command -v python3 >/dev/null 2>&1; then
            python3 "${REPO_ROOT}/scripts/validate-steam-libretro-cores.py" \
                "${DEPOT_ROOT}/usr/share/240mp/vendor/retroarch/cores" \
                "${approved_file}" \
                || fail "Bundled libretro core ABI validation failed"
        else
            echo "WARNING: python3 is unavailable; libretro ABI checks were skipped." >&2
        fi
    fi

    for excluded_core in \
        genesis_plus_gx_libretro.so \
        picodrive_libretro.so \
        snes9x_libretro.so \
        fbneo_libretro.so \
        mame2000_libretro.so \
        mame2003_libretro.so \
        mame2003_plus_libretro.so; do
        if [ -e "${DEPOT_ROOT}/usr/share/240mp/vendor/retroarch/cores/${excluded_core}" ]; then
            fail "Restricted core must not be bundled: ${excluded_core}"
        fi
    done

    if command -v sha256sum >/dev/null 2>&1; then
        if [ -f "${DEPOT_ROOT}/THIRD_PARTY_NOTICES/retroarch-cores/CORE-SHA256SUMS.txt" ]; then
            (
                cd "${DEPOT_ROOT}/usr/share/240mp/vendor/retroarch/cores"
                sha256sum -c \
                    "${DEPOT_ROOT}/THIRD_PARTY_NOTICES/retroarch-cores/CORE-SHA256SUMS.txt"
            ) || fail "Bundled core SHA-256 inventory does not match"
        fi
        if [ -f "${DEPOT_ROOT}/THIRD_PARTY_NOTICES/retroarch-cores/SOURCE-SHA256SUMS.txt" ]; then
            (
                cd "${DEPOT_ROOT}/THIRD_PARTY_NOTICES/retroarch-cores/sources"
                sha256sum -c ../SOURCE-SHA256SUMS.txt
            ) || fail "Bundled core-source SHA-256 inventory does not match"
        fi
        if [ -f "${DEPOT_ROOT}/THIRD_PARTY_NOTICES/retroarch-cores/SUPPORT-SHA256SUMS.txt" ]; then
            (
                cd "${DEPOT_ROOT}/usr/share/240mp/vendor/retroarch/system"
                sha256sum -c \
                    "${DEPOT_ROOT}/THIRD_PARTY_NOTICES/retroarch-cores/SUPPORT-SHA256SUMS.txt"
            ) || fail "Bundled core-support SHA-256 inventory does not match"
        fi
    fi
fi

if [ "${STRICT_PORTABLE}" = "1" ]; then
    if [ ! -d "${DEPOT_ROOT}/usr/plugins/platforms" ]; then
        fail "Qt platform plugins are not bundled under usr/plugins/platforms"
    fi
    if [ ! -d "${DEPOT_ROOT}/usr/qml/QtQuick" ]; then
        fail "Qt Quick modules are not bundled under usr/qml/QtQuick"
    fi
    require_file "usr/lib/libSDL3.so.0"
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
        candidate_dir="$(dirname "${candidate}")"
        candidate_library_path="${candidate_dir}/lib:${candidate_dir}:${library_path}"
        ldd_output="$(LD_LIBRARY_PATH="${candidate_library_path}" ldd "${candidate}" 2>&1 || true)"
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

while IFS= read -r generated_game_archive; do
    fail "Generated game archive must not be shipped: ${generated_game_archive#${DEPOT_ROOT}/}"
done < <(find "${DEPOT_ROOT}" -type f \
    \( -iname 'mm.o2r' -o -iname 'mm.otr' -o -iname 'mm.zip' \
       -o -iname 'oot.o2r' -o -iname 'oot-mq.o2r' -o -iname 'oot.otr' \) -print)

if [ "${STEAM_ALLOW_CONTENT_FILES:-0}" != "1" ]; then
    while IFS= read -r content_file; do
        case "${content_file#${DEPOT_ROOT}/}" in
            "usr/share/240mp/vendor/retroarch/system/bluemsx/Machines/MSX - C-BIOS/cbios_logo_msx1.rom" \
            |"usr/share/240mp/vendor/retroarch/system/bluemsx/Machines/MSX - C-BIOS/cbios_main_msx1.rom")
                continue
                ;;
        esac
        fail "Possible ROM, BIOS, or disc image in depot: ${content_file#${DEPOT_ROOT}/}"
    done < <(find "${DEPOT_ROOT}" -type f \
        \( -iname '*.nes' -o -iname '*.sfc' -o -iname '*.smc' \
           -o -iname '*.gba' -o -iname '*.gb' -o -iname '*.gbc' \
           -o -iname '*.a26' -o -iname '*.a52' -o -iname '*.a78' \
           -o -iname '*.cue' -o -iname '*.chd' -o -iname '*.iso' \
           -o -iname '*.rom' -o -iname '*.z64' -o -iname '*.n64' \
           -o -iname '*.v64' -o -iname '*.wad' -o -iname '*.m3u' \) -print)
fi

if [ "${failures}" -ne 0 ]; then
    echo "Steam depot validation failed with ${failures} error(s)." >&2
    exit 1
fi

echo "Steam depot validation passed: ${DEPOT_ROOT}"
