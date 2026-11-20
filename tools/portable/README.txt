Pinball Port Creator - Windows / Python only

1. Extract this ZIP.
2. Double-click "Create Game.pyw".
3. Choose a destination if you want, then click "Create Game".
4. When it finishes, click "Open game folder".

REQUIREMENTS

Windows x64 and normal 64-bit Python 3.10 or newer with Tkinter.
Internet access is needed for the first XP-file download.
Allow about50MB of free space for the tool, cache and game folders.

Docker, WSL, compilers, CMake, and a separately installed7-Zip are not needed.
The verified game and a portable archive reader are included. Nothing is
installed system-wide and no administrator access is required.

WHAT THE BUTTON DOES

The creator downloads the selected Internet Archive copy of the XP files,
checks their exact hashes, extracts the original art/font/sound, and combines
them with our verified precompiled game. It creates:

  Xbox360/default.xex - console display setting
  Xenia/default.xex   - Xenia display setting
  Source.zip          - matching GPLv3 source and build scripts
  creation-report.json - hashes and creation result

Keep each game folder together. Original PINBALL.EXE is never run or put in
the finished game. New output folders preserve earlier games. Local XP
folders, ZIPs and the supported7z are also accepted if preferred. Choose
"Use files I already have", paste the path into the editable box (or click
"Paste path"), or use "Choose ZIP / 7z". "Choose folder" is a separate
option for extracted files. Windows Copy-as-path quotes are accepted.

This creates a ready game; it does not compile C++ on the user's computer.
Source is included for developers. BUILD-FROM-SOURCE.txt explains the
separate developer setup for changing/recompiling game code.

CACHE

Only the small XP download is cached under
%LOCALAPPDATA%\PinballPortCreator. No compiler environment is downloaded.
Advanced optional overrides: PINBALL_BUILDER_CACHE and
PINBALL_BUILDER_OUTPUT. Defaults require no configuration.

LICENSES

New game and creator: GPL-3.0-only. Read LICENSE and LICENSE-SCOPE.txt.
The included 7zr reader comes from the public-domain LZMA SDK26.06; its
notice, official source archive and hashes are retained in payload/.
Other game runtime notices remain beside each executable.

Original XP assets retain separate rights. The source package contains no
original XP art/font/audio/executable. Download availability and this GPL
do not grant permission to redistribute original XP assets.

The selected download is:
https://archive.org/details/3-d-pinball-for-windows.-7z

Privacy: no account, telemetry, upload, credentials or console access.
The creator downloads files and prepares local folders only. Build tools
are not searched for or used during game creation.
