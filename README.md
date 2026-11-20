# Pinball Port Creator

Create a playable **Space Cadet Pinball port for Xbox 360 and Xenia** with a
simple Python window.

The Creator combines the included, tested game executable with separately
downloaded or user-supplied XP game assets. It does not compile C++ during
normal game creation.

## Quick start

1. Download and extract the **Windows Creator ZIP** from this project's Releases.
2. Double-click **Create Game.pyw**.
3. Choose a destination and click **Create Game**.
4. Click **Open game folder** when it finishes.

You need **Windows x64 and 64-bit Python 3.10 or newer with Tkinter**.
The standard Windows Python installer includes Tkinter. Internet access is
needed for the first automatic XP-file download; allow about 50 MB free space.
No compiler, WSL, Docker, separate 7-Zip installation or administrator access
is needed for the Creator.

If you prefer local files, choose **Use files I already have**. Paste a path,
choose a ZIP/7z archive, or use the separate folder picker. The supported XP
release is version 5.1.2600.5512; expected hashes are checked.

## Your game folders

- **Xbox360/default.xex**: for an RGH/JTAG Xbox 360.
- **Xenia/default.xex**: for Xenia.
- **Source.zip**: matching GPLv3 source and developer build instructions.
- **creation-report.json**: the creation result and file identities.

Keep each Xbox360 or Xenia folder together, including its assets and notices.
The console and Xenia packages use different display settings. Game version
0.0.15 has been played successfully in both environments. Full 1:1 parity
with XP is not claimed; high-score persistence, multiplayer and MIDI music
remain outside this version.

## Controls

| Input | Action |
| --- | --- |
| LT / RT | Left / right flipper |
| A or D-pad Down | Plunger |
| LB / RB | Shake the table |
| Start | Pause |
| Y | New game |

Enter **Up, Up, Down, Down, Left, Right, Left, Right, B, A** to unlock the
cheat menu for the current session. Afterward, Back opens or closes it.

## Downloads and privacy

The default XP download comes from the selected
[Internet Archive item](https://archive.org/details/3-d-pinball-for-windows.-7z).
The exact archive and required file hashes are verified. The original
PINBALL.EXE is read for a font resource; it is never executed or included
in the finished game.

Downloads are cached under %LOCALAPPDATA%\PinballPortCreator.
The default output parent is %USERPROFILE%\Downloads\Pinball Port Games.
Each creation uses a new output folder. There are no accounts, telemetry,
uploads or console connections.

## Source and development

The repository exposes the new game code in **src/rewrite**, tests in
**tests/rewrite**, and Creator/build tools in **tools/portable**.
The downloadable Creator includes the same code in **payload/GameSource.zip**.

For changing or recompiling the C++ game, read
[BUILD-FROM-SOURCE.txt](tools/portable/BUILD-FROM-SOURCE.txt).
That developer workflow needs a separate build environment; it is not part
of normal one-button creation. **build.py** and **Start Builder.pyw** are the
developer entry points. A source-only archive intentionally excludes the
precompiled payload needed by **Create Game.pyw**; use the Creator ZIP for
normal game creation.

## License and asset rights

Newly authored code is **GPL-3.0-only**. See [LICENSE](LICENSE) and
[LICENSE-SCOPE.txt](LICENSE-SCOPE.txt).

Original XP graphics, table data, fonts, audio and cover art are **not included**
in this repository or Creator download and retain separate rights. Download
availability and the GPL grant do not grant permission to redistribute those
assets or the completed game folders.

External runtime components keep their licenses and notices under
**tools/portable/runtime-notices**. The portable archive reader's notice,
official source archive and provenance are under **payload/**.
