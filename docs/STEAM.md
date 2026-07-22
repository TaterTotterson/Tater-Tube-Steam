# Steam desktop port

The Steam release is a Linux x86_64 desktop build of the same open-source
Tater Tube application. It is not a Raspberry Pi image and does not fork the
product name or module names.

## Current boundary

The `TATER_TUBE_STEAM` CMake option changes distribution behavior while keeping
the shared application and QML module code:

- PC Link remains available through a bundled Moonlight runtime.
- Game Center remains available through bundled RetroArch and approved cores.
- RetroNAS remains a first-class Game Center source. Steam builds use a bundled
  rclone SMB client and an unprivileged, read-only FUSE mount instead of the
  Pi-only `sudo mount.cifs` helper. If FUSE is unavailable, Tater Tube catalogs
  the share with rclone and downloads only the selected game into a 20 GB local
  cache. Referenced CUE/BIN and M3U files are fetched together. A user-selected
  local ROM folder remains available as a second fallback.
- The Pi-only RetroArch core installer is not exposed.
- Tater Tube's self-updater is disabled; Steam owns app updates.
- Pi service, SSH, Bluetooth-service, and Argon fan controls are not exposed.
  Controller mapping remains available; controller pairing is handled by Steam.
- The companion HTTP API is disabled by default. A user can opt in with
  `MP240_API_ENABLED=1`; its default Steam bind address is `127.0.0.1`.
- mpv, Moonlight, RetroArch, cores, yt-dlp, and rclone can be discovered under
  the depot's `usr/share/240mp/vendor` directory.

## Build

The reproducible release path builds inside Valve's official Steam Linux
Runtime 3.0 (sniper) SDK image. The image is digest-pinned and installs a
digest-pinned Qt and linuxdeploy toolchain:

```bash
./scripts/build-steam-sniper.sh
```

For a fast compile-only development build inside any x86_64 Linux environment:

```bash
./scripts/build-steam-linux.sh
```

The staged depot is written to `out/steam/depot`. In Steamworks, set the Linux
launch executable to:

```text
./launch-tater-tube.sh
```

For a portable depot outside the sniper builder, set `LINUXDEPLOY` to the
linuxdeploy executable and make the linuxdeploy Qt plugin available:

```bash
LINUXDEPLOY=/path/to/linuxdeploy ./scripts/build-steam-linux.sh
```

Set `STEAM_REQUIRE_COMPLETE_RUNTIME=1` in release automation so a build fails
until mpv, Moonlight, RetroArch, approved cores, yt-dlp, rclone, the pinned
sm64coopdx, 2 Ship 2 Harkinian, Ship of Harkinian, and SpaghettiKart port
engines, and all third-party notices are present in their expected paths.

Vetted runtime bundles can be injected without checking binaries into the
source tree:

```bash
STEAM_MPV_BUNDLE=/runtime/mpv \
STEAM_MOONLIGHT_BUNDLE=/runtime/moonlight-sdl \
STEAM_RETROARCH_BUNDLE=/runtime/retroarch \
STEAM_YTDLP_BUNDLE=/runtime/yt-dlp \
STEAM_RCLONE_BUNDLE=/runtime/rclone \
STEAM_PORTS_BUNDLE=/runtime/ports \
STEAM_THIRD_PARTY_NOTICES_DIR=/runtime/notices \
STEAM_REQUIRE_COMPLETE_RUNTIME=1 \
LINUXDEPLOY=/tools/linuxdeploy \
./scripts/build-steam-linux.sh
```

The pinned official x86-64 rclone and yt-dlp bundles can be prepared in a
sibling runtime directory:

```bash
./scripts/prepare-steam-static-tools.sh ../Tater-Tube-Steam-Runtime
```

The script verifies release-asset SHA-256 hashes and records the versions,
source URLs, and license notices.

Build mpv, Moonlight, RetroArch, the approved x86-64 core bundle, and the
patched native port engines:

```bash
./scripts/build-steam-runtimes.sh ../Tater-Tube-Steam-Runtime
```

The build pins source revisions for mpv, FFmpeg, libplacebo, Moonlight,
RetroArch, and every core. Approved cores are written to `retroarch/cores`.
sm64coopdx is built from its pinned v1.5.1 commit in an Ubuntu 22.04 container,
with the Tater Tube exit-menu patch, and is written to `ports/sm64coopdx`.
2 Ship 2 Harkinian is built from pinned 4.0.2 source in the Steam Runtime
builder and is written to `ports/2ship2harkinian`. Its patch automatically uses
the ROM Game Center has validated, keeps the generated `mm.o2r` under the
user's writable data directory, and provides **Exit to Tater Tube**.
Ship of Harkinian is built from pinned 9.2.3 source and is written to
`ports/shipwright`. Its patch processes Game Center's validated ROM without a
file picker, keeps `oot.o2r` or `oot-mq.o2r` in writable user data, and labels
its power action **Exit to Tater Tube**. The engine bundle includes its pinned
source information and available submodule licenses. Shipwright does not publish
a project-level license in its repository, so retain the confirmed upstream
redistribution permission with the Steam release records.
SpaghettiKart is built from pinned 1.0.0 source and is written to
`ports/spaghettikart`. Its patch imports Game Center's validated Mario Kart 64
(USA) ROM without a file picker, keeps the generated `mk64.o2r` in writable user
data, enables controller navigation, and provides **Exit to Tater Tube**. Only
the port-owned `spaghetti.o2r` is bundled. SpaghettiKart does not publish a
project-level license in its repository, so retain the confirmed redistribution
permission with the Steam release records.
Starship is built from pinned 2.0.0 source and is written to `ports/starship`.
Its patch imports Game Center's validated Star Fox 64 USA 1.0 or 1.1 ROM
without a file picker, keeps the generated `sf64.o2r` in writable user data,
enables controller navigation, and provides **Exit to Tater Tube**. The release
includes only the CC0 engine, extraction metadata, and port-owned
`starship.o2r`.
Dusklight is built from pinned 1.4.1 CC0 source and is written to
`ports/dusklight`. Its patch labels the controller-accessible quit menu
**Exit to Tater Tube**. Game Center streams a supported user-owned GameCube USA
or EUR Twilight Princess disc image directly from the configured library; the
engine performs its own game/supported-region check and no disc content enters the
depot. Dusklight supports ISO/GCM, CISO, GCZ, NFS, RVZ, WBFS, WIA, and TGC
containers and uses Vulkan on SteamOS.

The RetroArch cores' corresponding-source archives, upstream license files,
manifest, and SHA-256 inventories are written beneath `notices/retroarch-cores` and enter
the depot with the binaries. The source packager excludes common ROM, disc,
game-data, and firmware extensions so unrelated upstream test assets cannot
silently enter the customer depot. Each core is compiled from that repacked
archive rather than from the unfiltered download.

Use the prepared tools in a Sniper development depot with:

```bash
STEAM_MPV_BUNDLE=../Tater-Tube-Steam-Runtime/mpv \
STEAM_MOONLIGHT_BUNDLE=../Tater-Tube-Steam-Runtime/moonlight-sdl \
STEAM_RETROARCH_BUNDLE=../Tater-Tube-Steam-Runtime/retroarch \
STEAM_RCLONE_BUNDLE=../Tater-Tube-Steam-Runtime/rclone \
STEAM_YTDLP_BUNDLE=../Tater-Tube-Steam-Runtime/yt-dlp \
STEAM_PORTS_BUNDLE=../Tater-Tube-Steam-Runtime/ports \
STEAM_THIRD_PARTY_NOTICES_DIR=../Tater-Tube-Steam-Runtime/notices \
./scripts/build-steam-sniper.sh
```

Each bundle is copied beneath its matching `vendor` directory. The rclone
bundle must contain `bin/rclone`. The RetroArch bundle must include the approved
cores under `cores/`. When linuxdeploy is enabled, the app plus the bundled mpv,
Moonlight, and RetroArch executables are all scanned for shared-library
dependencies. The notices directory must contain:

```text
mpv.txt
moonlight-sdl.txt
retroarch.txt
retroarch-cores.txt
yt-dlp.txt
rclone.txt
qt.txt
APPROVED-CORES.txt
```

`APPROVED-CORES.txt` contains one exact `*_libretro.so` filename per line. The
depot validator rejects any bundled core not present in that list.
Sniper builds also copy Qt's official SPDX documents for Qt Base, Declarative,
and SVG into `THIRD_PARTY_NOTICES/qt-sbom`. Every depot includes Tater Tube's
`LICENSE.txt`, exact source revision, and corresponding-source location.

The Steam replacements intentionally omit the Pi edition's personal or
non-commercial cores. bsnes replaces Snes9x, BlastEm replaces Genesis Plus GX
for supported Mega Drive/Genesis cartridge formats, Geolith handles `.neo` and
Neo Geo CD content, and current MAME handles arcade and zipped Neo Geo sets.
There is currently no bundled Sega CD or Sega 32X replacement. BlastEm does
not accept SMD images in this build, and current MAME may require ROM sets
matching its newer database rather than old MAME 2000/2003 sets.
The distributable blueMSX databases and C-BIOS machine are bundled and seeded
to the user's writable RetroArch system directory; proprietary BIOS files are
never included.

Run the validator directly against an existing depot with:

```bash
STEAM_REQUIRE_COMPLETE_RUNTIME=1 \
STEAM_REQUIRE_PORTABLE_DEPOT=1 \
./scripts/validate-steam-depot.sh out/steam/depot
```

It checks required runtimes, Qt plugins, x86-64 ELF architecture, unresolved
shared libraries, core approvals, notices, leaked credentials, and common
ROM/disc-image extensions. The build also writes `DEPOT-SHA256SUMS.txt`.

## Runtime layout

```text
depot/
├── launch-tater-tube.sh
├── LICENSE.txt
├── SOURCE.txt
├── BUILD-COMMIT.txt
└── usr/
    ├── bin/tater-tube
    ├── lib/
    ├── plugins/
    ├── qml/
    └── share/240mp/
        ├── Main.qml
        ├── assets/
        ├── modules/
        ├── scripts/
        └── vendor/
            ├── mpv/bin/mpv
            ├── moonlight-sdl/bin/moonlight
            ├── retroarch/bin/retroarch
            ├── retroarch/cores/
            ├── ports/sm64coopdx/
            ├── ports/2ship2harkinian/
            ├── ports/shipwright/
            ├── ports/spaghettikart/
            ├── ports/starship/
            ├── ports/dusklight/
            ├── yt-dlp/bin/yt-dlp
            └── rclone/bin/rclone
```

## Steamworks

Example ContentBuilder manifests live in `packaging/steam/steamworks`. Copy
them without the `.example` suffix and replace the app and depot placeholders.
Keep `preview` enabled until the private test branch has passed.

After Steam assigns the numeric IDs, generate preview-mode manifests without
editing the checked-in templates:

```bash
STEAM_APP_ID=123456 \
STEAM_LINUX_DEPOT_ID=123457 \
./scripts/prepare-steamworks-manifests.sh
```

The private manifests are written to `out/steamworks`, which is ignored by Git.

Required store and library artwork is under `assets/steam/store`. Rebuild and
validate it with:

```bash
./scripts/build-steam-assets.sh
```

Store screenshots are separate 1920x1080 captures from the real application;
do not substitute generated or composited UI. Re-capture them from the staged
depot with:

```bash
./scripts/capture-steam-screenshots.sh
```

Marketing artwork and screenshots are source/release materials and are
explicitly excluded from the customer depot.

The shipped application deliberately does not link the Steamworks SDK and does
not use Steam DRM. Valve's ContentBuilder upload tool stays outside the depot;
Valve documents that the SDK is required for uploading but its application
features are optional. This also keeps the Steam binary identical in principle
to a build distributed outside Steam.

Valve separately warns that copyleft licenses such as GPL can be problematic
when code is combined with the Steamworks SDK. Before publishing, review the
Steam Distribution Agreement and the final dependency/license inventory with
qualified counsel. Any future Steam Input, Rich Presence, DRM, or SDK-linked
feature needs a new GPL compatibility review before it is merged.

## Release gates

Before uploading a public build:

1. Build in Valve's current Linux Runtime SDK and test in Steam Deck Gaming
   Mode plus a desktop Linux Steam client.
2. Bundle every required shared library and QML/Qt plugin; validate the depot
   on a clean machine.
3. Record license notices and exact source revisions for every bundled runtime.
4. Confirm the pinned core manifest and bundled license/source inventory still
   match the release. Free price and open source do not override core licenses.
5. Complete the GPL/Steam Distribution Agreement rights review without assuming
   that the app being free or open source resolves Steamworks compatibility.
6. Test controller-only navigation, suspend/resume, display scaling, video
   playback, Sunshine pairing, RetroNAS mounting/reconnection, and
   launching/returning from RetroArch.
7. Test RetroNAS in both FUSE and download-cache modes, including guest and
   authenticated SMB, sleep/resume, network loss, CUE/BIN games, and cache
   eviction.
8. Review store-page wording for YouTube and Newznab features and make clear
   that users must supply and be authorized to access their own services and
   content.
