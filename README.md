# OpenBRC

OpenBRC is an open source reimplementation of **Barbie Riding Club** (Mattel Media /
Human Code, 1998) for modern Linux, Windows and macOS.

The goal is preservation: the game should look, sound and play as it did in 1998. On top
of that, OpenBRC adds:

- any resolution, aspect ratio and refresh rate, windowed or fullscreen, HiDPI aware;
  the original 640×480 picture is scaled sharply, with optional smooth and CRT filters
- a Vulkan renderer with an automatic OpenGL fallback
- native builds for Linux, Windows 10/11 (with an installer) and macOS 13+ (Intel and Apple Silicon)

> **Status:** in development. See [docs/status.md](docs/status.md) for what works.

## You need the original game

OpenBRC contains **no** Barbie Riding Club content: no pictures, animations, movies,
sounds, music or text. It reads everything from your own copy of the game, which can be
any of:

- the original CD-ROM in a drive (or a copy of its contents)
- a disc image: `.iso`, `.bin`/`.cue` or `.img`
- an installation of the original game (the folder with `Program` and `Resources`); a
  partial installation is combined with the disc, as the original game did

OpenBRC never runs or modifies the original program. It checks that the files are those
of the Barbie Riding Club CD-ROM v1.0 (volume `BRCV1_0`) and refuses to start without
them.

*Barbie* is a trademark of Mattel, Inc. This project is not affiliated with or endorsed by
Mattel or Human Code.

## Playing

On first start OpenBRC looks for the game disc, a disc image in your Downloads folder or
an installation, and asks you to choose one if it finds none. You can also pass it on the
command line:

```sh
openbrc --source ~/Downloads/BRCV1_0.ISO       # use this disc image (and remember it)
openbrc --check-source /run/media/$USER/BRCV1_0  # is this usable?
openbrc --setup                                # choose the game disc again
```

| Key | Effect |
|---|---|
| Alt+Enter, F11 | Fullscreen / windowed |
| F10 | Picture: 4:3 with borders, stretched, or whole-number scale |
| F9 | Filter: sharp, pixelated, smooth, CRT |
| F8 | VSync on/off |
| F12 | Screenshot |

Settings are stored in `openbrc.ini`; saved games, photos and printouts in the data folder:

| Platform | Settings | Saved games, photos, logs |
|---|---|---|
| Linux | `~/.config/openbrc` | `~/.local/share/openbrc` |
| Windows | `%APPDATA%\OpenBRC` | `%LOCALAPPDATA%\OpenBRC` |
| macOS | `~/Library/Application Support/OpenBRC` | same |
| Portable (Windows ZIP) | `user\` next to `openbrc.exe` | same |

The game's printing feature (certificates, photos) saves pages as PNG images in the
`Printouts` folder instead of sending them to a printer.

## Building from source

Requirements: CMake ≥ 3.25, Ninja, a C++23 compiler (GCC 14+, Clang 18+, MSVC 2022 17.10+,
Apple Clang 16+). SDL3, the Vulkan headers and the volk loader are fetched automatically
when not installed. No shader compiler is needed (shaders are precompiled).

```sh
cmake -S . -B build -G Ninja
cmake --build build
./build/bin/openbrc --source /path/to/BRCV1_0.ISO
```

Tests: `OPENBRC_GAME_DATA=/path/to/BRCV1_0.ISO ctest --test-dir build` (tests that need
the game skip themselves without it).

Packages: `cpack` in the build directory makes a `.tar.gz` (Linux), `.dmg` (macOS) or a
portable `.zip` (Windows); `cmake --build build --target package_installer` makes the
Windows installer (needs NSIS). A Windows build can also be cross-compiled from Linux with
MinGW-w64: `-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-x86_64.cmake`.

## How it works

The original game is built on the **Open Media Toolkit** (OMT, a C++ multimedia framework
by Yves Schmid, later released under the LGPL) and a game layer by Human Code. OpenBRC
re-implements both in modern C++:

- `formats/` reads the original data: OMT containers (`.omt`), bitmaps, sprites,
  animations, compiled scripts, sounds and Smacker movies.
- `engine/` is the runtime the game was written against: scenes ("nodes"), animations,
  the script interpreter, sound channels, movies, the palette and the cursor.
- `game/` holds the scenes themselves: the stable, tack room, grooming, trail rides, the
  race, the mini-games and so on.
- `render/` shows the 8-bit 640×480 picture on modern displays with Vulkan or OpenGL.

The behaviour of every part was worked out from the original executable with
[REA](https://github.com/morluto/rea) and Ghidra, the OMT sources, and the BarbieRecomp
static recompilation. The notes are in [docs/](docs/); [docs/testing.md](docs/testing.md) explains
the unit tests and the scripted, headless runs used to compare scenes with the original.

## Licence

GPL-3.0-or-later. See [LICENSE](LICENSE).
