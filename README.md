# SW2NES

A NES emulator for the Nintendo Switch 2, running as a homebrew app from
**hbmenu** under [kaiHEN](https://github.com/ouncebrew/kaihen). It is native
code (C++ with OpenGL ES), built with devkitPro and the Switch 2 devkit
([ounce-devkit](https://github.com/ouncebrew/ounce-devkit)).

Two emulation cores are included: **Nestopia UE** (accurate, supports nearly
every mapper; the default) and **QuickNES** (fast and light).

No game ROMs are included or provided.

> SW2NES started as an app for the CivHomebrew launcher. That version (1.1.1)
> is no longer maintained; its source is kept in `legacy/civhomebrew/`.

## Features

- Nestopia UE or QuickNES core, switchable in Settings
- Low input latency: one frame in flight, run-ahead (1–2 frames), frame delay at 60 Hz
- 120 Hz / VRR on the Switch 2 screen, where the game is shown at the NES's own
  60.1 Hz rate (the display mode is read from the system; 60 Hz elsewhere)
- Rewind (hold ZL) and fast-forward (hold or toggle ZR)
- GPU shaders: CRT-Royale (lite), CRT-Geom, CRT-Lottes, CRT-Easymode, CRT-Aperture,
  zfast CRT, LCD grid, Scale2x, sharp bilinear, bilinear
- NTSC signal simulation (composite, composite with dot crawl, S-Video)
- Curvature, rounded corners, vignette and brightness boost
- Aspect ratio (1:1, 8:7, 4:3, Stretch), integer or fit scaling, overscan
- Colour palettes, volume and audio filter
- 4 save-state slots per game, battery saves, optional auto-load of the latest state
- Two players, with separate button mapping and turbo for each
- Game Genie codes (on-screen editor, or a `.gg` text file next to the ROM)
- Favourites, recently played and time played per game
- Zipped ROMs (`.zip` with a `.nes` inside)
- Per-game or global settings; advanced options can be hidden

## Installing

1. Set up kaiHEN on your Switch 2 so that hbmenu runs.
2. Copy `sw2nes.nro` from the [latest release](https://github.com/Rodpad/SW2NES/releases/latest)
   to `/switch/sw2nes/sw2nes.nro` on the SD card.
3. Copy your games (`.nes` or `.zip`) to `/roms/nes` on the SD card. SW2NES
   creates this folder on its first launch. A different folder can be chosen in
   Settings > ROM folder.
4. Launch **SW2NES** from hbmenu.

Settings, saves and play time are kept in `/switch/sw2nes/` on the SD card.
Game Genie codes can also be put next to a game as `<game name>.gg`, one code per
line with an optional description, for example `SXIOPO Infinite lives`.

## Controls

| Switch 2 | NES / app |
| --- | --- |
| D-pad / left stick | D-pad |
| A, B | NES A (default) |
| X, Y | NES B (default) |
| + / − | Start / Select |
| L + R | In-game menu (press again to close) |
| ZL (hold) | Rewind |
| ZR | Fast-forward |

Face buttons can be remapped per player in Settings, including turbo.

## Building

Requirements: [devkitPro pacman](https://devkitpro.org/wiki/devkitPro_pacman) with
`switch-dev`, `switch-freetype` and `switch-zlib`, and the Switch 2 devkit
(`sh install.sh` from ounce-devkit).

```sh
export DEVKITPRO=/opt/devkitpro
make -C hbmenu
```

This builds `hbmenu/sw2nes.nro`.

## Layout

| Path | Contents |
| --- | --- |
| `hbmenu/source/` | The app: menus, settings, game loop, video (OpenGL ES), audio, input |
| `hbmenu/romfs/shaders/` | Picture shaders (GLSL ES) and the NTSC decoder |
| `native/bridge.cpp` | C interface to QuickNES (frames, audio, states, palettes) |
| `native/bridge_nestopia.cpp` | The same interface for Nestopia UE |
| `third_party/QuickNES_Core` | QuickNES, with SW2NES patches (marked `SW2NES_`) |
| `third_party/nestopia` | Nestopia UE core (from libretro/nestopia, see `SOURCE.txt`) |
| `licenses/` | Licences of the bundled components (also inside the app) |
| `legacy/civhomebrew/` | The former CivHomebrew version (1.1.1, unmaintained) |

## Licence

SW2NES is free software under the GNU General Public License v3 or later (see
`LICENSE`). It includes:

- Nestopia UE, GPL-2.0-or-later
- QuickNES (Shay Green / blargg; libretro QuickNES_Core), LGPL-2.1-or-later
- Roboto (Apache 2.0) and Source Code Pro (SIL OFL 1.1) fonts
- libounce and libnx (ISC), libkaihen (ISC), OunceVK / Mesa (MIT), mimalloc (MIT),
  FreeType (FTL), HarfBuzz (MIT), libpng, zlib, minizip (zlib licence) and bzip2

Their licence texts are in `licenses/`.
