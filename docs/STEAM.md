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
until mpv, Moonlight, RetroArch, approved cores, yt-dlp, rclone, and all
third-party notices are present in their expected paths.

Vetted runtime bundles can be injected without checking binaries into the
source tree:

```bash
STEAM_MPV_BUNDLE=/runtime/mpv \
STEAM_MOONLIGHT_BUNDLE=/runtime/moonlight-sdl \
STEAM_RETROARCH_BUNDLE=/runtime/retroarch \
STEAM_YTDLP_BUNDLE=/runtime/yt-dlp \
STEAM_RCLONE_BUNDLE=/runtime/rclone \
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
source URLs, and license notices. mpv, Moonlight, RetroArch, and every selected
core remain separate build-and-license work because their dependency and core
licenses must be reviewed as a unit.

Use the prepared tools in a Sniper development depot with:

```bash
STEAM_RCLONE_BUNDLE=../Tater-Tube-Steam-Runtime/rclone \
STEAM_YTDLP_BUNDLE=../Tater-Tube-Steam-Runtime/yt-dlp \
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
            ├── yt-dlp/bin/yt-dlp
            └── rclone/bin/rclone
```

## Steamworks

Example ContentBuilder manifests live in `packaging/steam/steamworks`. Copy
them without the `.example` suffix and replace the app and depot placeholders.
Keep `preview` enabled until the private test branch has passed.

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
4. Include only RetroArch cores whose copyright holders have explicitly allowed
   this distribution. Free price and open source do not override core licenses.
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
