# Install Tater Tube on Steam Deck

The complete Linux build is distributed through GitHub and installs as a
non-Steam application. It includes Game Center, RetroNAS support, RetroArch,
the bundled cores, PC Link, and the native game-port engines.

## Recommended installation

1. Switch the Steam Deck to Desktop Mode.
2. Download
   [Install-Tater-Tube.desktop](https://github.com/TaterTotterson/Tater-Tube-Steam/releases/latest/download/Install-Tater-Tube.desktop).
3. Open the downloaded file and approve the prompt to execute it.
4. Leave the installer window open while it downloads and extracts the
   complete release.
5. Return to Gaming Mode and open **Library > Non-Steam > Tater Tube**.

The installer:

- verifies the release archive with SHA-256;
- installs entirely beneath the current user's home directory;
- creates a Tater Tube application-menu entry;
- asks SteamOS to add that entry to the non-Steam library;
- installs the Tater Tube grid, portrait, hero, logo, and icon artwork for the
  non-Steam library entry; and
- keeps configuration, pairings, saves, and the Game Center cache separate
  from the replaceable application files.

The current complete archive is roughly 700 MB compressed and expands to about
1.5 GB. Keep at least 4 GB free during installation or an update so the old
copy can remain available until the new copy has been verified and activated.

## If the Steam shortcut is not added automatically

In Desktop Mode, open Steam and select **Games > Add a Non-Steam Game to My
Library**. Choose **Tater Tube** from the application list. If it is not listed,
browse to:

```text
~/.local/bin/tater-tube
```

Return to Gaming Mode after adding it. If directional controls work but the
buttons do not, open Tater Tube's Steam **Controller Settings**, select
**Templates > Gamepad**, and apply the layout.

The artwork is applied automatically when SteamOS creates the shortcut. If the
shortcut is added manually after installation, run the installer again. An
update does not add a duplicate shortcut, but it detects the existing shortcut
and attaches the included artwork to it.

## Updating

Open **Settings > System > Check For Updates** inside Tater Tube. When a newer
GitHub release is available, choose **Install Update**. Tater Tube downloads and
verifies the release, preserves user data, and restarts into the new version.

Installations older than the built-in updater can run
`Install-Tater-Tube.desktop` again once to enable it. The installer replaces
only the application under:

```text
~/.local/share/tater-tube/app
```

User settings and caches remain under:

```text
~/.local/share/240-MP
```

An update does not add a duplicate Steam shortcut. To request the shortcut
again, run:

```bash
curl -fL \
  https://github.com/TaterTotterson/Tater-Tube-Steam/releases/latest/download/install-steam-deck.sh \
  | bash -s -- --add-to-steam
```

## Terminal installation

The same installer can be run without the desktop launcher:

```bash
curl -fL -o /tmp/install-tater-tube.sh \
  https://github.com/TaterTotterson/Tater-Tube-Steam/releases/latest/download/install-steam-deck.sh
chmod +x /tmp/install-tater-tube.sh
/tmp/install-tater-tube.sh
```

No `sudo` command is required or supported.
