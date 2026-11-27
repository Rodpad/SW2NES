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

### Updating

Replace `/switch/sw2nes/sw2nes.nro` with the one from the newest release. Settings,
saves, save states and play time are kept in `/switch/sw2nes/` and are not touched
by an update. (Close SW2NES on the console before uploading over FTP; a running
app's file cannot be replaced.)

## Features

- **Box art browsing, scraped on the console:** browse your games as a grid of box covers or as a
  list, with a details panel showing the box in 3D (front, back and sides; drag to turn it), the
  title, publisher, year, genre, region and description. Box art and details are downloaded on
  the console from [ScreenScraper](https://www.screenscraper.fr), with no account needed and no PC
  required (Settings > App > Download missing box art and descriptions; the app offers it the first
  time games have none). Four views (List, List & details, Grid, Grid & details) on the right stick
  press; with details downloaded, the list shows each game's proper title
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
- Game list with All / Favourites / Recent tabs, search, sorting (name, last played, date added),
  Last Played dates and time played; games are found in subfolders of the ROM folder too
- Themes: Dark, Light, Black, Blue, Purple, Red, Green, Orange, Retro and Retro Colour
- Touch on every screen (tap, hold to highlight, drag to scroll), with an on-screen Menu button
  during play; UI size Normal (larger, easier to tap) or Compact
- Button remapping screen per player (all games, or one game), including turbo and stick directions
- Single Joy-Cons held sideways work as a controller, with position-based button hints
- Game Genie codes (on-screen editor, or a `.gg` text file next to the ROM)
- Zipped ROMs (`.zip` with a `.nes` inside)
- Per-game or global settings; advanced options can be hidden

## Controls

| Switch 2 | NES / app |
| --- | --- |
| D-pad / left stick | D-pad |
| B, X | NES A (default) |
| A, Y | NES B (default) |
| + / − | Start / Select |
| L + R + stick press | In-game menu (press again to close); SL + SR + stick press on a single Joy-Con |
| ZL (hold) | Rewind |
| ZR | Fast-forward |

Buttons can be remapped per player in Settings > Sound & controls (or for one game from
the in-game Controls tab), including turbo. A single Joy-Con held sideways: its bottom and top
buttons are A, right and left B, + or − Start, SL Select and SR Start.

In the game list:

| Switch 2 | Game list |
| --- | --- |
| A | Play the highlighted game |
| L / R | All / Favourites / Recent |
| ZL / ZR | Page up / down (hold to keep paging) |
| Left stick press | Next sort order (name, last played, date added) |
| Right stick press | Next view: List & details, List, Grid & details, Grid |
| − / + | Favourite / Search |
| Y / X | Refresh / Settings |
| B | Exit (press twice) |

The list and the grid wrap round: past the first or last game, the D-pad stops on the header row
(tabs, search, sort) or the footer buttons, then carries on round. On a single Joy-Con, SL switches
tabs and SR pages down. Everything can also be tapped; drag to scroll, flick to glide.

## Building

Requirements: [devkitPro pacman](https://devkitpro.org/wiki/devkitPro_pacman) with
`switch-dev`, `switch-freetype`, `switch-zlib`, `switch-curl` and `switch-mbedtls`, and the Switch 2 devkit
(`sh install.sh` from ounce-devkit).

```sh
export DEVKITPRO=/opt/devkitpro
make -C hbmenu
```

This builds `hbmenu/sw2nes.nro`.

Box art downloads use [ScreenScraper](https://www.screenscraper.fr) and need a ScreenScraper
developer account, which is not in this repository: put `devid=...` and `devpassword=...` on two
lines in `~/.config/sw2/screenscraper.txt` and run `tools/make_ss_dev.sh`, which writes the
ignored `hbmenu/source/ss_dev.h`. Without it the app builds and runs but cannot download box art.

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
- curl (curl licence), Mbed TLS (Apache 2.0), stb_image (MIT / public domain) and the
  Mozilla CA certificates (MPL 2.0), for downloading box art and game details

- Date formats from the Unicode CLDR (Unicode licence)

Their licence texts are in `licenses/`.
