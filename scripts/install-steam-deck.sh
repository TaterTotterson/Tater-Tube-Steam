#!/usr/bin/env bash
# Install or update the portable Tater Tube Linux build for one user.
set -Eeuo pipefail

readonly REPOSITORY="TaterTotterson/Tater-Tube-Steam"
readonly DEFAULT_RELEASE_ASSET_BASE="https://github.com/${REPOSITORY}/releases/latest/download"

ADD_TO_STEAM="auto"
SHOW_DIALOGS="yes"
RELEASE_ASSET_BASE="${TATER_TUBE_RELEASE_ASSET_BASE:-${DEFAULT_RELEASE_ASSET_BASE}}"
INSTALL_BASE="${TATER_TUBE_INSTALL_BASE:-${XDG_DATA_HOME:-${HOME}/.local/share}/tater-tube}"
BIN_DIR="${TATER_TUBE_BIN_DIR:-${HOME}/.local/bin}"
APPLICATIONS_DIR="${TATER_TUBE_APPLICATIONS_DIR:-${XDG_DATA_HOME:-${HOME}/.local/share}/applications}"
CACHE_BASE="${TATER_TUBE_CACHE_BASE:-${XDG_CACHE_HOME:-${HOME}/.cache}/tater-tube-installer}"
APP_DIR="${INSTALL_BASE}/app"
ARTWORK_DIR="${INSTALL_BASE}/steam-artwork"
LAUNCHER="${BIN_DIR}/tater-tube"
DESKTOP_FILE="${APPLICATIONS_DIR}/com.tatertotterson.TaterTube.desktop"

usage() {
    cat <<'EOF'
Usage: install-steam-deck.sh [--add-to-steam|--no-add-to-steam|--in-app-update]

Installs or updates Tater Tube under the current user's home directory. The
default install does not require sudo and preserves settings in
~/.local/share/240-MP.

Options:
  --add-to-steam     Ask Steam to add the installed launcher even on an update.
  --no-add-to-steam  Do not ask Steam to add a non-Steam shortcut.
  --in-app-update    Update an existing installation without desktop dialogs.
  -h, --help         Show this help.

Test/advanced environment variables:
  TATER_TUBE_ARCHIVE_PATH       Use an existing release archive.
  TATER_TUBE_ARTWORK_PATH       Use an existing Steam artwork archive.
  TATER_TUBE_CHECKSUM_PATH      Use an existing SHA256SUMS.txt.
  TATER_TUBE_RELEASE_ASSET_BASE Override the latest-release asset URL.
  TATER_TUBE_INSTALL_BASE       Override the user-level install directory.
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --add-to-steam)
            ADD_TO_STEAM="yes"
            ;;
        --no-add-to-steam)
            ADD_TO_STEAM="no"
            ;;
        --in-app-update)
            ADD_TO_STEAM="no"
            SHOW_DIALOGS="no"
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
    shift
done

die() {
    echo "ERROR: $*" >&2
    if [ "${SHOW_DIALOGS}" = "yes" ] \
            && command -v kdialog >/dev/null 2>&1 \
            && { [ -n "${DISPLAY:-}" ] || [ -n "${WAYLAND_DISPLAY:-}" ]; }; then
        kdialog --title "Tater Tube Installer" --error "$*" >/dev/null 2>&1 || true
    fi
    exit 1
}

require_command() {
    command -v "$1" >/dev/null 2>&1 || die "Required command not found: $1"
}

if [ "$(id -u)" -eq 0 ]; then
    die "Run this installer as the normal desktop user, not with sudo."
fi
if [ "$(uname -s)" != "Linux" ] || [ "$(uname -m)" != "x86_64" ]; then
    die "This installer requires 64-bit x86 Linux (Steam Deck/SteamOS or desktop Linux)."
fi

for command in awk curl find sha256sum sort tar zstd; do
    require_command "${command}"
done

mkdir -p "${CACHE_BASE}"
RUN_DIR="$(mktemp -d "${CACHE_BASE}/run.XXXXXX")"
cleanup() {
    case "${RUN_DIR:-}" in
        "${CACHE_BASE}"/run.*)
            rm -rf -- "${RUN_DIR}"
            ;;
    esac
}
trap cleanup EXIT

CHECKSUM_FILE="${RUN_DIR}/SHA256SUMS.txt"
if [ -n "${TATER_TUBE_CHECKSUM_PATH:-}" ]; then
    cp -- "${TATER_TUBE_CHECKSUM_PATH}" "${CHECKSUM_FILE}"
else
    echo "Downloading the latest release manifest..."
    curl -fL --retry 3 --progress-bar \
        -o "${CHECKSUM_FILE}" "${RELEASE_ASSET_BASE}/SHA256SUMS.txt"
fi

archive_name="$({
    awk '
        {
            name = $2
            sub(/^\*/, "", name)
            if (name ~ /^Tater-Tube-Steam-v[0-9]+\.[0-9]+\.[0-9]+([-A-Za-z0-9.]*)-linux-x86_64\.tar\.zst$/) {
                print name
                exit
            }
        }
    ' "${CHECKSUM_FILE}"
})"
[ -n "${archive_name}" ] || die "The release manifest does not name a Tater Tube Linux archive."

artwork_archive_name="$({
    awk '
        {
            name = $2
            sub(/^\*/, "", name)
            if (name == "Tater-Tube-Steam-Artwork.tar.gz") {
                print name
                exit
            }
        }
    ' "${CHECKSUM_FILE}"
})"
[ -n "${artwork_archive_name}" ] \
    || die "The release manifest does not name the Steam artwork archive."

ARCHIVE_FILE="${RUN_DIR}/${archive_name}"
if [ -n "${TATER_TUBE_ARCHIVE_PATH:-}" ]; then
    cp -- "${TATER_TUBE_ARCHIVE_PATH}" "${ARCHIVE_FILE}"
else
    echo "Downloading ${archive_name}..."
    echo "The current complete build is a large download; keep the Deck awake."
    curl -fL --retry 3 --progress-bar \
        -o "${ARCHIVE_FILE}" "${RELEASE_ASSET_BASE}/${archive_name}"
fi

expected_sha="$({
    awk -v target="${archive_name}" '
        {
            name = $2
            sub(/^\*/, "", name)
            if (name == target) {
                print $1
                exit
            }
        }
    ' "${CHECKSUM_FILE}"
})"
[ -n "${expected_sha}" ] || die "No checksum was found for ${archive_name}."
actual_sha="$(sha256sum "${ARCHIVE_FILE}" | awk '{print $1}')"
[ "${actual_sha}" = "${expected_sha}" ] \
    || die "The downloaded archive failed its SHA-256 check."

ARTWORK_ARCHIVE_FILE="${RUN_DIR}/${artwork_archive_name}"
if [ -n "${TATER_TUBE_ARTWORK_PATH:-}" ]; then
    cp -- "${TATER_TUBE_ARTWORK_PATH}" "${ARTWORK_ARCHIVE_FILE}"
else
    echo "Downloading Steam library artwork..."
    curl -fL --retry 3 --progress-bar \
        -o "${ARTWORK_ARCHIVE_FILE}" \
        "${RELEASE_ASSET_BASE}/${artwork_archive_name}"
fi

expected_artwork_sha="$({
    awk -v target="${artwork_archive_name}" '
        {
            name = $2
            sub(/^\*/, "", name)
            if (name == target) {
                print $1
                exit
            }
        }
    ' "${CHECKSUM_FILE}"
})"
[ -n "${expected_artwork_sha}" ] \
    || die "No checksum was found for ${artwork_archive_name}."
actual_artwork_sha="$(sha256sum "${ARTWORK_ARCHIVE_FILE}" | awk '{print $1}')"
[ "${actual_artwork_sha}" = "${expected_artwork_sha}" ] \
    || die "The downloaded Steam artwork failed its SHA-256 check."

artwork_roots="$({
    tar -tzf "${ARTWORK_ARCHIVE_FILE}" \
        | awk -F/ 'NF && $1 != "." { print $1 }' \
        | sort -u
})"
[ "${artwork_roots}" = "Tater-Tube-Steam-Artwork" ] \
    || die "The Steam artwork archive has an unexpected layout."

release_root="$({
    tar --zstd -tf "${ARCHIVE_FILE}" \
        | awk -F/ 'NF && $1 != "." { print $1 }' \
        | sort -u
})"
case "${release_root}" in
    Tater-Tube-Steam-v*-linux-x86_64)
        ;;
    *)
        die "The release archive has an unexpected root directory: ${release_root}"
        ;;
esac

EXTRACT_DIR="${RUN_DIR}/extract"
mkdir -p "${EXTRACT_DIR}"
echo "Extracting Tater Tube..."
tar --zstd -xf "${ARCHIVE_FILE}" -C "${EXTRACT_DIR}"
STAGED_APP="${EXTRACT_DIR}/${release_root}"
[ -x "${STAGED_APP}/launch-tater-tube.sh" ] \
    || die "The release is missing launch-tater-tube.sh."
[ -x "${STAGED_APP}/usr/bin/tater-tube" ] \
    || die "The release is missing the Tater Tube executable."

first_install="yes"
[ -d "${APP_DIR}" ] && first_install="no"
mkdir -p "${INSTALL_BASE}" "${BIN_DIR}" "${APPLICATIONS_DIR}"

NEW_APP="${INSTALL_BASE}/app.new.$$"
PREVIOUS_APP="${INSTALL_BASE}/app.previous.$$"
for path in "${NEW_APP}" "${PREVIOUS_APP}"; do
    case "${path}" in
        "${INSTALL_BASE}"/app.new.*|"${INSTALL_BASE}"/app.previous.*)
            rm -rf -- "${path}"
            ;;
        *)
            die "Refusing to replace an unexpected path: ${path}"
            ;;
    esac
done

mv -- "${STAGED_APP}" "${NEW_APP}"
if [ -d "${APP_DIR}" ]; then
    mv -- "${APP_DIR}" "${PREVIOUS_APP}"
fi
if ! mv -- "${NEW_APP}" "${APP_DIR}"; then
    [ ! -d "${PREVIOUS_APP}" ] || mv -- "${PREVIOUS_APP}" "${APP_DIR}"
    die "Could not activate the new Tater Tube installation."
fi
rm -rf -- "${PREVIOUS_APP}"

NEW_ARTWORK="${INSTALL_BASE}/steam-artwork.new.$$"
PREVIOUS_ARTWORK="${INSTALL_BASE}/steam-artwork.previous.$$"
for path in "${NEW_ARTWORK}" "${PREVIOUS_ARTWORK}"; do
    case "${path}" in
        "${INSTALL_BASE}"/steam-artwork.new.*|"${INSTALL_BASE}"/steam-artwork.previous.*)
            rm -rf -- "${path}"
            ;;
        *)
            die "Refusing to replace an unexpected artwork path: ${path}"
            ;;
    esac
done
mkdir -p "${NEW_ARTWORK}"
tar -xzf "${ARTWORK_ARCHIVE_FILE}" \
    --strip-components=1 \
    -C "${NEW_ARTWORK}"
for required_artwork in \
    wide-grid.png \
    portrait-grid.png \
    hero.png \
    logo.png \
    icon.png; do
    [ -f "${NEW_ARTWORK}/${required_artwork}" ] \
        || die "The Steam artwork archive is missing ${required_artwork}."
done
if [ -d "${ARTWORK_DIR}" ]; then
    mv -- "${ARTWORK_DIR}" "${PREVIOUS_ARTWORK}"
fi
if ! mv -- "${NEW_ARTWORK}" "${ARTWORK_DIR}"; then
    [ ! -d "${PREVIOUS_ARTWORK}" ] \
        || mv -- "${PREVIOUS_ARTWORK}" "${ARTWORK_DIR}"
    die "Could not activate the Steam library artwork."
fi
rm -rf -- "${PREVIOUS_ARTWORK}"

launcher_tmp="${LAUNCHER}.tmp.$$"
printf '#!/usr/bin/env bash\nset -euo pipefail\nexport TATER_TUBE_LAUNCHER=%q\nexec %q "$@"\n' \
    "${LAUNCHER}" "${APP_DIR}/launch-tater-tube.sh" > "${launcher_tmp}"
chmod 0755 "${launcher_tmp}"
mv -- "${launcher_tmp}" "${LAUNCHER}"

desktop_exec="${LAUNCHER// /\\ }"
desktop_path="${APP_DIR// /\\ }"
desktop_icon="${APP_DIR// /\\ }/usr/share/240mp/assets/images/logo.svg"
desktop_tmp="${DESKTOP_FILE}.tmp.$$"
cat > "${desktop_tmp}" <<EOF
[Desktop Entry]
Type=Application
Name=Tater Tube
Comment=Retro media center for the TV
Exec=${desktop_exec}
Path=${desktop_path}
Icon=${desktop_icon}
Terminal=false
Categories=AudioVideo;Game;
StartupNotify=true
EOF
chmod 0644 "${desktop_tmp}"
mv -- "${desktop_tmp}" "${DESKTOP_FILE}"
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "${APPLICATIONS_DIR}" >/dev/null 2>&1 || true
fi

request_steam_add="no"
case "${ADD_TO_STEAM}" in
    yes)
        request_steam_add="yes"
        ;;
    auto)
        [ "${first_install}" = "no" ] || request_steam_add="yes"
        ;;
esac

steam_result=""
if [ "${request_steam_add}" = "yes" ]; then
    if command -v steamos-add-to-steam >/dev/null 2>&1; then
        if steamos-add-to-steam "${DESKTOP_FILE}" >/dev/null 2>&1; then
            steam_result=" SteamOS was asked to add it to the non-Steam library."
        else
            steam_result=" Add Tater Tube manually from Steam's Games menu."
        fi
    else
        steam_result=" Add Tater Tube manually from Steam's Games menu."
    fi
fi

install_steam_artwork() {
    command -v python3 >/dev/null 2>&1 || return 2

    python3 - "${ARTWORK_DIR}" <<'PY'
import glob
import os
import shutil
import struct
import sys


artwork_dir = sys.argv[1]


def read_cstring(data, offset):
    end = data.index(b"\0", offset)
    return data[offset:end].decode("utf-8", "replace"), end + 1


def read_object(data, offset):
    value = {}
    while offset < len(data):
        value_type = data[offset]
        offset += 1
        if value_type == 8:
            return value, offset

        key, offset = read_cstring(data, offset)
        if value_type == 0:
            item, offset = read_object(data, offset)
        elif value_type == 1:
            item, offset = read_cstring(data, offset)
        elif value_type == 2:
            item = struct.unpack_from("<i", data, offset)[0]
            offset += 4
        elif value_type in (3, 4, 6):
            item = data[offset:offset + 4]
            offset += 4
        elif value_type in (7, 10):
            item = data[offset:offset + 8]
            offset += 8
        else:
            raise ValueError(f"Unsupported binary VDF value type: {value_type}")
        value[key] = item
    return value, offset


def find_shortcut_ids(shortcuts_path):
    with open(shortcuts_path, "rb") as stream:
        root, _ = read_object(stream.read(), 0)

    shortcuts = next(
        (value for key, value in root.items()
         if key.lower() == "shortcuts" and isinstance(value, dict)),
        {},
    )
    app_ids = []
    for shortcut in shortcuts.values():
        if not isinstance(shortcut, dict):
            continue
        fields = {key.lower(): value for key, value in shortcut.items()}
        if str(fields.get("appname", "")).strip().lower() != "tater tube":
            continue
        app_id = fields.get("appid")
        if isinstance(app_id, int):
            app_ids.append(app_id & 0xFFFFFFFF)
    return app_ids


steam_roots = [
    os.path.expanduser("~/.local/share/Steam"),
    os.path.expanduser("~/.steam/steam"),
    os.path.expanduser("~/.var/app/com.valvesoftware.Steam/.local/share/Steam"),
]
shortcut_files = []
for steam_root in steam_roots:
    shortcut_files.extend(glob.glob(
        os.path.join(steam_root, "userdata", "*", "config", "shortcuts.vdf")
    ))

artwork = {
    "": "wide-grid.png",
    "p": "portrait-grid.png",
    "_hero": "hero.png",
    "_logo": "logo.png",
    "_icon": "icon.png",
}
installed = set()
for shortcuts_path in sorted(set(map(os.path.realpath, shortcut_files))):
    try:
        app_ids = find_shortcut_ids(shortcuts_path)
    except (OSError, ValueError, struct.error):
        continue
    grid_dir = os.path.join(os.path.dirname(shortcuts_path), "grid")
    for app_id in app_ids:
        key = (os.path.realpath(grid_dir), app_id)
        if key in installed:
            continue
        os.makedirs(grid_dir, exist_ok=True)
        for suffix, source_name in artwork.items():
            source = os.path.join(artwork_dir, source_name)
            destination = os.path.join(grid_dir, f"{app_id}{suffix}.png")
            temporary = destination + ".tmp"
            shutil.copyfile(source, temporary)
            os.replace(temporary, destination)
        installed.add(key)

if not installed:
    sys.exit(3)
print(len(installed))
PY
}

artwork_result=""
artwork_attempts=1
[ "${request_steam_add}" = "no" ] || artwork_attempts=15
artwork_status=3
while [ "${artwork_attempts}" -gt 0 ]; do
    if artwork_count="$(install_steam_artwork 2>/dev/null)"; then
        artwork_status=0
        artwork_result=" Steam library artwork was installed."
        break
    else
        artwork_status=$?
    fi
    artwork_attempts=$((artwork_attempts - 1))
    if [ "${artwork_status}" -eq 3 ] && [ "${artwork_attempts}" -gt 0 ]; then
        sleep 1
    else
        break
    fi
done
if [ "${artwork_status}" -eq 2 ]; then
    artwork_result=" Steam artwork is available in ${ARTWORK_DIR}; Python 3 is required to attach it automatically."
elif [ "${artwork_status}" -eq 3 ]; then
    artwork_result=" Steam artwork is installed locally and will be attached when the Steam shortcut is added again."
elif [ "${artwork_status}" -ne 0 ]; then
    artwork_result=" Steam artwork is installed locally but could not be attached automatically."
fi

installed_version="${release_root#Tater-Tube-Steam-}"
installed_version="${installed_version%-linux-x86_64}"
message="Tater Tube ${installed_version} is installed.${steam_result}${artwork_result}

Return to Gaming Mode and open Library > Non-Steam. If Steam Deck buttons do not respond, apply Templates > Gamepad in Controller Settings."

echo
echo "${message}"
if [ "${SHOW_DIALOGS}" = "yes" ] \
        && command -v kdialog >/dev/null 2>&1 \
        && { [ -n "${DISPLAY:-}" ] || [ -n "${WAYLAND_DISPLAY:-}" ]; }; then
    kdialog --title "Tater Tube Installer" --msgbox "${message}" >/dev/null 2>&1 || true
fi
