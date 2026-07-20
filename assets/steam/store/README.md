# Steam graphical assets

Run `./scripts/build-steam-assets.sh` to reproduce the files under `generated/`.
The validator enforces Valve's current pixel dimensions and the transparent
library-logo requirement.

## Sources

- `source/retro-media-room-landscape.png` is original generated background
  artwork for horizontal capsules and the library hero.
- `source/retro-media-room-portrait.png` is original generated background
  artwork for vertical capsules and icons.
- `assets/images/tater-tube-readme.png` supplies the exact existing Tater Tube
  wordmark and seated mascot.
- `assets/images/mascots/game-center.png` supplies the exact existing Game
  Center mascot.

The generated backgrounds intentionally contain no wording, brand marks,
third-party characters, or simulated application UI. The Steam library hero is
background artwork only. Product wording and characters are composited from
the repository's existing transparent brand assets.

## Generation briefs

Landscape:

> Wide 16:9 cinematic artwork of a cozy dark retro media room centered on a
> vintage CRT television with warm orange static, analog VCR and stereo
> equipment, deep black and burnt-orange palette, soft volumetric glow,
> realistic premium game-store key art, clean central composition with room
> for a logo and character overlay, no people, no mascots, no text, no logos,
> no symbols, and no user interface.

Portrait:

> Tall 2:3 companion artwork of the same cozy dark retro media room, centered
> vintage CRT with warm orange static and stacked analog equipment, deep black
> and burnt-orange palette, cinematic light, realistic premium game-store key
> art, open upper and lower composition for a logo and character overlay, no
> people, no mascots, no text, no logos, no symbols, and no user interface.

## Upload mapping

| File | Steam use |
| --- | --- |
| `header-capsule.png` | Store header capsule |
| `small-capsule.png` | Store small capsule |
| `main-capsule.png` | Store main capsule |
| `vertical-capsule.png` | Store vertical capsule |
| `library-capsule.png` | Library capsule |
| `library-hero.png` | Library hero |
| `library-logo.png` | Transparent library logo |
| `library-header.png` | Library header |
| `shortcut-icon.png` | Client shortcut icon |
| `app-icon.jpg` | Store app icon |

Store screenshots are not generated artwork. They must be 1920x1080 captures
from the real application and must show only features present in the submitted
build. Valve currently requires at least five product screenshots.

After building the depot, capture repeatable Linux screenshots in a clean
temporary data directory with:

```bash
./scripts/capture-steam-screenshots.sh
```

The script drives the real packaged application in the sniper SDK and writes
the captures under `screenshots/`. Captures must still receive a human review
before upload, especially after any UI or feature change.

## Trailer

Valve requires at least one trailer. After building the depot, capture a
30-second real-application first-look video with:

```bash
./scripts/capture-steam-trailer.sh
```

The ignored output at `out/steam/trailer/tater-tube-first-look.mp4` is
1920x1080, 30 fps H.264 with an AAC stereo track. Review the complete video
before upload and re-capture it after any visible UI or launch-feature change.
