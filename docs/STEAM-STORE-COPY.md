# Steam store-page draft

This is launch-oriented source copy for the free Linux desktop edition. Keep
the Steamworks page limited to features that pass on the uploaded build. Remove
the Game Center and RetroNAS claims if an approved core is not included before
review.

## Product name

Tater Tube

## Short description

Turn your Linux PC into a cozy retro media center for personal libraries,
local video, PC streaming, online playlists, and user-supplied retro games—all
inside a couch-friendly CRT interface.

## About this software

Tater Tube brings the feel of late-night television, tapes, and classic game
menus to a modern Linux PC. Browse your own media and services from one
couch-friendly interface with a warm CRT look.

### Your media, one channel guide

- Play local video and audio files.
- Connect to your own Emby, Jellyfin, or Plex library.
- Browse supported over-the-air and playlist sources.
- Stream a PC running Sunshine through PC Link.

### Game Center and RetroNAS

Launch user-supplied legal games through the bundled Game Center runtime. A
local ROM folder works directly, while RetroNAS can browse a read-only SMB
share through an unprivileged mount. If mounting is unavailable, Tater Tube
downloads only the selected game and its referenced files into a managed local
cache.

Tater Tube does not include games, ROMs, BIOS files, media, service
subscriptions, or access credentials. You are responsible for supplying and
being authorized to use your content and third-party services.

### Free and open source

The Steam edition is free, contains no advertising or analytics, and does not
require a Tater Tube account. Its optional companion HTTP service is disabled
by default and is restricted to the local computer when enabled.

## Basic-info selections

- Application type: Software is the closest fit unless the existing Steam app
  was intentionally registered as a game.
- Operating system: Linux only for the first release.
- Languages: English interface and subtitles only until localization is
  actually tested.
- Single-player / multiplayer: do not claim multiplayer; PC Link streams a
  user's separate PC session.
- Controller support: leave unselected until controller-only navigation and
  text entry pass on the uploaded build and Steam Deck.
- Steam Cloud, achievements, leaderboards, Workshop, trading cards, VR, and
  Remote Play: do not select.
- Anti-cheat: none.
- In-app purchases: none.
- Third-party account: none required by Tater Tube. Optional media services may
  require the user's own account.

## Draft Linux system requirements

### Minimum

- Requires a 64-bit processor and operating system
- OS: 64-bit Linux with the Steam client
- Processor: Dual-core x86-64 processor
- Memory: 4 GB RAM
- Graphics: OpenGL 3.3 or Vulkan-capable graphics
- Network: Broadband connection for network and streaming features
- Storage: 2 GB available space, plus space for user media and the optional
  RetroNAS cache
- Additional notes: External media services, a Sunshine host, and a RetroNAS
  share are optional and are not included

### Recommended

- Requires a 64-bit processor and operating system
- OS: SteamOS 3 or a current 64-bit desktop Linux distribution
- Processor: Quad-core x86-64 processor
- Memory: 8 GB RAM
- Graphics: Vulkan-capable graphics with hardware video decoding
- Network: Wired Ethernet for high-bitrate media and PC streaming
- Storage: 22 GB available space when using the default 20 GB RetroNAS cache

## Screenshot captions and alt text

1. Home menu — “Tater Tube’s CRT-style home screen with media, Game Center,
   local-file, and PC Link choices.”
2. Game Center — “Game Center highlighted in the main menu beside its original
   controller mascot.”
3. RetroNAS setup — “Game Center’s RetroNAS connection form for an optional
   user-owned SMB library.”
4. PC Link — “PC Link’s Moonlight interface for pairing with a user-owned
   Sunshine host.”
5. Settings — “Tater Tube’s appearance, feature, gamepad, and system settings.”

## Final page checks

- Do not place external links in the written description; use Steam's
  dedicated website and social-link fields.
- Do not select a feature checkbox until that feature works in the uploaded
  build.
- Keep all five screenshots as real application captures without marketing
  text.
- Upload at least one real-application trailer. The repeatable 1080p draft is
  generated at `out/steam/trailer/tater-tube-first-look.mp4`.
- Preview the complete page in Store Beta Mode before publishing it.
