#!/usr/bin/env bash
# Build the reviewed Linux x86_64 libretro core inventory from pinned sources.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
MANIFEST="${STEAM_CORE_MANIFEST:-${REPO_ROOT}/packaging/steam/core-manifest.tsv}"
GROUP="${1:-all}"
OUTPUT_ROOT="${2:-/opt/tater-tube-runtimes}"
BUILD_ROOT="${STEAM_CORE_BUILD_ROOT:-/build/libretro-cores}"
BUILD_JOBS="${STEAM_RUNTIME_BUILD_JOBS:-${BUILD_JOBS:-4}}"
CORE_ROOT="${OUTPUT_ROOT}/retroarch/cores"
SUPPORT_ROOT="${OUTPUT_ROOT}/retroarch/system"
NOTICE_ROOT="${OUTPUT_ROOT}/notices/retroarch-cores"
SOURCE_ROOT="${NOTICE_ROOT}/sources"
LICENSE_ROOT="${NOTICE_ROOT}/licenses"
MARKER_ROOT="${NOTICE_ROOT}/built"
SOURCE_EXCLUDES=(
    '*.7z' '*.32x' '*.a' '*.a26' '*.a52' '*.a78' '*.atr' '*.atx'
    '*.bin' '*.car' '*.cas' '*.cbn' '*.ccd' '*.cdf' '*.chd' '*.col'
    '*.com' '*.cue' '*.dcm' '*.dmg' '*.dsk' '*.dylib' '*.exe' '*.fds'
    '*.fig' '*.gb' '*.gba' '*.gbc' '*.gen' '*.gg' '*.img' '*.int'
    '*.ipk3' '*.iso' '*.iwad' '*.lmp' '*.lnx' '*.m3u' '*.md' '*.mdf'
    '*.min' '*.mvc' '*.mx1' '*.mx2' '*.neo' '*.nes' '*.ngc' '*.ngp'
    '*.ngpc' '*.npc' '*.o' '*.o2' '*.pak' '*.pbp' '*.pc2' '*.pce'
    '*.pk3' '*.pwad' '*.ri' '*.rom' '*.sc' '*.sf' '*.sfc' '*.sg'
    '*.sgb' '*.sgx' '*.smc' '*.smd' '*.sms' '*.so' '*.toc' '*.unf'
    '*.unif' '*.vb' '*.vec' '*.wad' '*.ws' '*.wsc' '*.xfd' '*.xex'
    '*.zip'
)

if [ ! -f "${MANIFEST}" ]; then
    echo "Core manifest does not exist: ${MANIFEST}" >&2
    exit 1
fi

for command in cmake curl file make strip tar; do
    if ! command -v "${command}" >/dev/null 2>&1; then
        echo "Missing core build command: ${command}" >&2
        exit 1
    fi
done

mkdir -p "${BUILD_ROOT}" "${CORE_ROOT}" "${SUPPORT_ROOT}" "${SOURCE_ROOT}" \
    "${LICENSE_ROOT}" "${MARKER_ROOT}"
cp "${MANIFEST}" "${NOTICE_ROOT}/CORE-MANIFEST.tsv"

copy_license_files() {
    local source_dir="$1"
    local core_id="$2"
    local license_file relative destination
    local copied=0

    while IFS= read -r -d '' license_file; do
        relative="${license_file#${source_dir}/}"
        destination="${LICENSE_ROOT}/${core_id}/$(dirname "${relative}")"
        mkdir -p "${destination}"
        cp "${license_file}" "${destination}/"
        copied=$((copied + 1))
    done < <(
        find "${source_dir}" -maxdepth 5 -type f \
            \( -iname 'copying*' -o -iname 'license*' \
               -o -iname 'copyright*' \) -print0
    )

    if [ "${copied}" -eq 0 ]; then
        echo "No license file was found in the ${core_id} source archive." >&2
        exit 1
    fi
}

strip_core() {
    local core="$1"
    if [ "${STEAM_CORE_STRIP:-1}" = "1" ]; then
        strip --strip-unneeded "${core}"
    fi
}

preserve_markdown_license_files() {
    local source_dir="$1"
    local license_file preserved_file

    while IFS= read -r -d '' license_file; do
        preserved_file="${license_file%.*}.txt"
        if [ ! -e "${preserved_file}" ]; then
            cp "${license_file}" "${preserved_file}"
        fi
    done < <(
        find "${source_dir}" -maxdepth 5 -type f \
            \( -iname 'copying*.md' -o -iname 'license*.md' \
               -o -iname 'copyright*.md' \) -print0
    )
}

package_corresponding_source() {
    local source_dir="$1"
    local source_archive="$2"
    local temporary_archive="${source_archive}.tmp"
    local -a exclude_args=()
    local pattern prohibited

    for pattern in "${SOURCE_EXCLUDES[@]}"; do
        exclude_args+=(--exclude="${pattern}")
    done

    preserve_markdown_license_files "${source_dir}"
    tar --create --gzip --file "${temporary_archive}" \
        --directory "${source_dir}" \
        --exclude-vcs --wildcards --ignore-case \
        --exclude='./android' --exclude='./android/*' \
        "${exclude_args[@]}" .
    cmake -E rename "${temporary_archive}" "${source_archive}"

    prohibited="$(
        tar -tzf "${source_archive}" \
            | grep -Ei '\.(7z|32x|a|a26|a52|a78|atr|atx|bin|car|cas|cbn|ccd|cdf|chd|col|com|cue|dcm|dmg|dsk|dylib|exe|fds|fig|gb|gba|gbc|gen|gg|img|int|ipk3|iso|iwad|lmp|lnx|m3u|md|mdf|min|mvc|mx1|mx2|neo|nes|ngc|ngp|ngpc|npc|o|o2|pak|pbp|pc2|pce|pk3|pwad|ri|rom|sc|sf|sfc|sg|sgb|sgx|smc|smd|sms|so|toc|unf|unif|vb|vec|wad|ws|wsc|xfd|xex|zip)$' \
            | sed -n '1,20p' || true
    )"
    if [ -n "${prohibited}" ]; then
        echo "Prohibited content remained in ${source_archive}:" >&2
        echo "${prohibited}" >&2
        exit 1
    fi
}

copy_core_support_files() {
    local source_dir="$1"
    local core_id="$2"
    local destination source

    case "${core_id}" in
        bluemsx)
            destination="${OUTPUT_ROOT}/retroarch/system/bluemsx"
            source="${source_dir}/system/bluemsx"
            if [ ! -d "${source}/Databases" ] \
                    || [ ! -d "${source}/Machines/MSX - C-BIOS" ]; then
                echo "blueMSX source is missing its redistributable support files." >&2
                exit 1
            fi
            cmake -E remove_directory "${destination}/Databases"
            cmake -E remove_directory "${destination}/Machines/MSX - C-BIOS"
            cmake -E make_directory "${destination}/Machines"
            cmake -E copy_directory \
                "${source}/Databases" "${destination}/Databases"
            cmake -E copy_directory \
                "${source}/Machines/MSX - C-BIOS" \
                "${destination}/Machines/MSX - C-BIOS"
            ;;
    esac
}

build_core() {
    local core_id="$1"
    local core_file="$2"
    local repository="$3"
    local revision="$4"
    local build_subdir="$5"
    local makefile="$6"
    local make_arguments="$7"
    local repository_archive="${repository%.git}/archive/${revision}.tar.gz"
    local source_archive="${SOURCE_ROOT}/${core_id}-${revision}.tar.gz"
    local upstream_archive="${BUILD_ROOT}/${core_id}-${revision}-upstream.tar.gz"
    local source_dir="${BUILD_ROOT}/${core_id}"
    local marker="${MARKER_ROOT}/${core_id}-${revision}"
    local source_marker="${MARKER_ROOT}/${core_id}-${revision}-source-v1"
    local build_dir output
    local -a make_options=(-j"${BUILD_JOBS}")
    local -a make_args=()

    if [ "${STEAM_CORE_RESUME:-0}" = "1" ] \
            && [ -f "${marker}" ] \
            && [ -f "${CORE_ROOT}/${core_file}" ] \
            && [ -f "${source_archive}" ]; then
        echo "Reusing completed ${core_file}"
        strip_core "${CORE_ROOT}/${core_file}"
        if [ ! -f "${source_marker}" ]; then
            cmake -E remove_directory "${source_dir}"
            mkdir -p "${source_dir}"
            tar -xzf "${source_archive}" -C "${source_dir}" --strip-components=1
            copy_core_support_files "${source_dir}" "${core_id}"
            package_corresponding_source "${source_dir}" "${source_archive}"
            cmake -E remove_directory "${source_dir}"
            touch "${source_marker}"
        fi
        return
    fi

    if [ "${STEAM_CORE_RESUME:-0}" = "1" ] \
            && [ -f "${source_marker}" ] \
            && [ -f "${source_archive}" ] \
            && [ -d "${source_dir}" ]; then
        echo "Resuming incomplete ${core_file} build"
    else
        echo "Building ${core_file} from ${repository}@${revision}"
        curl -fL --retry 3 "${repository_archive}" -o "${upstream_archive}"
        cmake -E remove_directory "${source_dir}"
        mkdir -p "${source_dir}"
        tar -xzf "${upstream_archive}" -C "${source_dir}" --strip-components=1
        copy_core_support_files "${source_dir}" "${core_id}"
        package_corresponding_source "${source_dir}" "${source_archive}"
        touch "${source_marker}"
        cmake -E remove_directory "${source_dir}"
        mkdir -p "${source_dir}"
        tar -xzf "${source_archive}" -C "${source_dir}" --strip-components=1
    fi

    build_dir="${source_dir}/${build_subdir}"
    if [ ! -f "${build_dir}/${makefile}" ]; then
        echo "Missing ${core_id} build file: ${build_dir}/${makefile}" >&2
        exit 1
    fi
    while IFS= read -r -d '' interrupted_object; do
        echo "Discarding interrupted zero-byte object: ${interrupted_object#${source_dir}/}"
        cmake -E rm -f "${interrupted_object}"
    done < <(find "${source_dir}" -type f -name '*.o' -size 0 -print0)
    if [ -n "${make_arguments}" ]; then
        read -r -a make_args <<<"${make_arguments}"
    fi
    if [ "${STEAM_CORE_VERBOSE:-0}" != "1" ]; then
        make_options+=(-s)
    fi

    make -C "${build_dir}" -f "${makefile}" \
        "${make_options[@]}" "${make_args[@]}"

    output="$(
        find "${source_dir}" -type f -name "${core_file}" -print -quit
    )"
    if [ -z "${output}" ] && [ "${core_id}" = "mame" ]; then
        output="$(
            find "${source_dir}" -type f -name 'mamearcade_libretro.so' \
                -print -quit
        )"
        if [ -n "${output}" ]; then
            echo "Installing MAME arcade subtarget as ${core_file}"
        fi
    fi
    if [ -z "${output}" ]; then
        echo "${core_id} did not produce ${core_file}" >&2
        exit 1
    fi
    if ! file -b "${output}" | grep -q 'ELF 64-bit.*x86-64'; then
        echo "${core_id} produced a non-x86_64 core: $(file -b "${output}")" >&2
        exit 1
    fi

    install -m 0644 "${output}" "${CORE_ROOT}/${core_file}"
    strip_core "${CORE_ROOT}/${core_file}"
    copy_license_files "${source_dir}" "${core_id}"
    cmake -E rm -f "${upstream_archive}"
    touch "${marker}"
}

matched=0
while IFS='|' read -r group core_id core_file repository revision license \
        build_subdir makefile make_arguments; do
    case "${group}" in
        ''|\#*) continue ;;
    esac
    if [ "${GROUP}" != "all" ] && [ "${group}" != "${GROUP}" ]; then
        continue
    fi
    matched=$((matched + 1))
    build_core "${core_id}" "${core_file}" "${repository}" "${revision}" \
        "${build_subdir}" "${makefile}" "${make_arguments}"
done < "${MANIFEST}"

if [ "${matched}" -eq 0 ]; then
    echo "No core manifest entries matched group: ${GROUP}" >&2
    exit 1
fi

(
    cd "${CORE_ROOT}"
    find . -maxdepth 1 -type f -name '*_libretro.so' -print0 \
        | sort -z \
        | xargs -0 sha256sum
) > "${NOTICE_ROOT}/CORE-SHA256SUMS.txt"

(
    cd "${SOURCE_ROOT}"
    find . -maxdepth 1 -type f -name '*.tar.gz' -print0 \
        | sort -z \
        | xargs -0 sha256sum
) > "${NOTICE_ROOT}/SOURCE-SHA256SUMS.txt"

(
    cd "${SUPPORT_ROOT}"
    find . -type f -print0 \
        | sort -z \
        | xargs -0 sha256sum
) > "${NOTICE_ROOT}/SUPPORT-SHA256SUMS.txt"

echo "Built ${matched} ${GROUP} core(s) into ${CORE_ROOT}"
