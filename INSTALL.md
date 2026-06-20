# Install 240-MP

240-MP is meant to be used as a ready-to-flash Raspberry Pi 4 appliance image for a CRT over composite video.

The release images are built for:

- Raspberry Pi 4
- Composite video output to a CRT, with separate NTSC and PAL image assets
- Emby/Jellyfin media servers
- Argon IR remote support
- GPIO IR receiver on GPIO23, physical pin 16
- Boot screen and automatic launch straight into 240-MP
- SSH debugging

There is no Plex support in this fork.

## Flash the ready-to-flash image

1. Open the [latest release](https://github.com/TaterTotterson/240-MP-Emby-Jelly/releases/latest).
2. Download the `image_...crt-ntsc...img.xz` asset for an NTSC CRT, or the `image_...crt-pal...img.xz` asset for a PAL CRT.
3. Flash it to an SD card with Raspberry Pi Imager, Balena Etcher, or `dd`.
4. Put the SD card in a Raspberry Pi 4.
5. Connect the Pi to the CRT using composite video.
6. Boot the Pi.

The image boots directly into 240-MP. The default login is `tater` / `pi` for SSH and recovery access.

If this image will leave your own network, build a custom image with a stronger password first.

## Default Hardware Setup

### Composite Video

The release page has separate ready-to-flash images for NTSC and PAL composite output through the Raspberry Pi 4 3.5mm AV jack. Pick the image that matches your CRT video standard.

Custom local image builds default to `PI_IMAGE_PROFILE=crt-ntsc`. Use `PI_IMAGE_PROFILE=crt-pal` for PAL.

Use a known-good Raspberry Pi compatible composite cable. The 3.5mm AV cable pinout matters; some camcorder-style cables swap video and audio.

### Argon IR Remote

The image enables GPIO IR support by default for the Argon IR Remote for Argon Raspberry Pi cases.

Default IR receiver wiring:

| IR receiver | Raspberry Pi |
| --- | --- |
| Data | GPIO23, physical pin 16 |
| VCC | 3.3V |
| GND | Ground |

The built-in keymap includes Argon remote scancodes. If your remote uses different scancodes, SSH into the Pi and run:

```bash
sudo 240mp-ir-learn
```

Then copy the printed scancodes into `/etc/rc_keymaps/240mp.toml` and reload:

```bash
sudo systemctl restart 240mp-ir-keymap.service
```

### Boot And Recovery

The image shows the 240-MP boot screen and starts the app automatically.

SSH is enabled by default. The Settings screen can toggle SSH on or off.

If the display is black or stuck, press `Ctrl+Alt+F2` to open the recovery console on `tty2`, log in, and inspect the service logs:

```bash
journalctl -u 240mp -b
```

To return to the app:

```bash
sudo systemctl start 240mp
```

## Emby/Jellyfin Setup

1. Boot into 240-MP.
2. Open the Video on Demand module.
3. Enter your local Emby or Jellyfin server URL.
4. Sign in.
5. Choose the libraries you want shown on the CRT UI.

240-MP supports Emby and Jellyfin only. Plex is not supported.

## Update An Existing 240-MP Image

From SSH:

```bash
bash <(curl -fsSL https://github.com/TaterTotterson/240-MP-Emby-Jelly/releases/latest/download/install.sh)
```

Then reboot:

```bash
sudo reboot
```

Settings and Emby/Jellyfin sign-in data are kept during app updates.

## Build A Custom Image

Use this when you want a different login password, SSH key, or image name.

Requirements on your build machine:

- Docker
- Git
- Enough free disk space for a Raspberry Pi OS image build

Build the default Raspberry Pi 4 composite/Argon IR image:

```bash
./scripts/build-pi-image.sh
```

Build a PAL composite image:

```bash
PI_IMAGE_PROFILE=crt-pal PI_IMAGE_NAME=240mp-pal ./scripts/build-pi-image.sh
```

Build with a stronger login password:

```bash
PI_FIRST_USER_PASS='change-this-password' ./scripts/build-pi-image.sh
```

Build with your SSH public key:

```bash
PI_FIRST_USER_PUBKEY=~/.ssh/id_ed25519.pub ./scripts/build-pi-image.sh
```

Useful image build options:

| Variable | Default | Description |
| --- | --- | --- |
| `PI_IMAGE_PROFILE` | `crt-ntsc` | Composite CRT profile. Use `crt-ntsc` or `crt-pal`; `hdmi` and `none` are developer options |
| `PI_IMAGE_NAME` | `240mp` | Output image name prefix |
| `PI_FIRST_USER_NAME` | `tater` | Normal login user |
| `PI_FIRST_USER_PASS` | `pi` | Password for the normal login user |
| `PI_FIRST_USER_PUBKEY` | unset | SSH public key string or path to a `.pub` file |
| `PI_ENABLE_SSH` | `1` | Enable SSH in the image |
| `PI_ENABLE_IR` | `1` | Enable GPIO IR receiver support |
| `PI_IR_GPIO_PIN` | `23` | GPIO pin used by the IR receiver data line |
| `PI_IR_PROTOCOL` | `nec` | IR protocol loaded by `ir-keytable` |
| `PI_ENABLE_BOOT_SPLASH` | `1` | Show the 240-MP boot screen |

The finished `.img.xz` lands in:

```text
.cache/pi-gen-arm64/deploy/
```

## Local Control API

240-MP starts a small HTTP playback-control API with the app. By default it listens on all network interfaces at port `24024`:

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

Optional runtime environment variables:

| Variable | Default | Description |
| --- | --- | --- |
| `MP240_API_ENABLED` | `1` | Set to `0` to disable the API |
| `MP240_API_HOST` | `0.0.0.0` | Bind address; use `127.0.0.1` for local-only |
| `MP240_API_PORT` | `24024` | HTTP port |
| `MP240_API_TOKEN` | unset | If set, callers must send `Authorization: Bearer <token>` or `X-240MP-Token: <token>` |
