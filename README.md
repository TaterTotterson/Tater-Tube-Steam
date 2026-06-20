# 240-MP

240-MP is a retro VCR-style Emby/Jellyfin media frontend for a Raspberry Pi 4 connected to a CRT over composite video.

This fork is focused on one appliance-style setup:

- Raspberry Pi 4
- CRT display over composite output
- Ready-to-flash SD card images
- Boot screen and automatic launch straight into 240-MP
- Argon IR remote support through a GPIO IR receiver on GPIO23
- Local Emby/Jellyfin browsing and playback
- NTSC and PAL composite image builds
- No Plex support

The easiest way to use it is to download the ready-to-flash NTSC or PAL `.img.xz` from the latest GitHub release, flash it to an SD card, and boot the Pi.

## Features

### Emby/Jellyfin
- Local Emby/Jellyfin server sign in
- Movies, TV Shows, and Other Videos library browsing
- Continue Watching and Resume
- Autoplay next episode
- Playlist and Collection support
- Audio and subtitle track selection
- Auto direct play with AV1-to-H.264 fallback
- Forced transcode quality options

### Local Files
- Browse folders on the Pi
- Play common video formats
- `m3u` and `m3u8` playlist support
- Loop and shuffle playback

### Appliance Image
- Boots straight into 240-MP
- Separate NTSC and PAL composite CRT images
- 240-MP boot screen
- SSH enabled for debugging
- Argon IR remote defaults
- GPIO IR receiver default: GPIO23, physical pin 16
- Analog audio defaults for the Pi composite/3.5mm setup

### Controls
- Keyboard navigation
- USB remote/controller navigation
- Argon IR remote support
- Media keys during playback
- Local HTTP playback-control API for companion apps

### Local Control API

240-MP includes a small HTTP API for companion apps and voice-assistant bridges. It is enabled by default on the Pi image at port `24024`.

```bash
curl http://240mp.local:24024/api/v1/status
```

Useful endpoints:

```text
GET  /api/v1/status
POST /api/v1/player/play-pause
POST /api/v1/player/pause
POST /api/v1/player/resume
POST /api/v1/player/stop
POST /api/v1/player/seek          {"position_ms": 60000} or {"offset_ms": 30000}
POST /api/v1/player/skip-forward  {"offset_ms": 30000}
POST /api/v1/player/skip-back     {"offset_ms": -10000}
POST /api/v1/player/volume-up
POST /api/v1/player/volume-down
POST /api/v1/player/mute
POST /api/v1/player/key           {"key": "LEFT", "repeat": 1}
```

Set `MP240_API_TOKEN` to require `Authorization: Bearer <token>` or `X-240MP-Token: <token>`. See [INSTALL.md](INSTALL.md#local-control-api) for the full API settings.

## Install

- [Flash the ready-to-flash Raspberry Pi image](INSTALL.md#flash-the-ready-to-flash-image)
- [Build a custom image](INSTALL.md#build-a-custom-image)
- [Development builds](BUILDING.md)

## Hardware Target

This project targets one setup: a Raspberry Pi 4 connected to a CRT over composite video, with Emby/Jellyfin media playback and Argon IR remote control. Release images are built for both NTSC and PAL composite CRTs.

## License

This project is licensed under the GNU General Public License v3.0. See [LICENSE](LICENSE) for the full text.

You are free to use, study, and modify this code. If you distribute a modified version, you must also distribute it under GPL-3.0 and make the source available.

## Credits

This project started as a fork of [anthonycaccese/240-MP](https://github.com/anthonycaccese/240-MP). This fork is focused on Raspberry Pi 4 composite CRT use, NTSC/PAL image builds, Emby/Jellyfin support, and Argon IR remote defaults.
