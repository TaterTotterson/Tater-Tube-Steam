# Steam Deck library artwork

Run `./scripts/build-steam-library-artwork.sh` to reproduce the five files
under `generated/`. The manual installer attaches them to Tater Tube's
non-Steam shortcut in SteamOS Gaming Mode.

## Sources

- `source/retro-media-room-landscape.png` is original generated background
  artwork for the wide grid and library hero.
- `source/retro-media-room-portrait.png` is original generated background
  artwork for the portrait grid and icon.
- `assets/images/tater-tube-readme.png` supplies the existing Tater Tube
  wordmark and seated mascot.
- `assets/images/mascots/game-center.png` supplies the existing Game Center
  mascot.

The generated backgrounds contain no third-party characters or simulated
application UI. The library hero is background artwork only. Product wording
and characters are composited from the repository's existing transparent brand
assets.

## Installer mapping

| File | Non-Steam library use |
| --- | --- |
| `library-capsule.png` | Portrait grid |
| `library-hero.png` | Library hero |
| `library-logo.png` | Transparent library logo |
| `library-header.png` | Wide grid |
| `shortcut-icon.png` | Shortcut icon |
