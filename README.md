# SW2NES

A NES emulator for the Nintendo Switch 2, packaged as a `.civapp` for
[CivHomebrew](https://github.com/XITRIX/CivHomebrew). It runs the
[QuickNES](https://github.com/libretro/QuickNES_Core) core inside
CivHomebrew's browser engine, which runs JavaScript only (no JIT,
WebAssembly or WebGL). The whole emulator is therefore compiled to plain
JavaScript, and the hottest parts are hand-written JavaScript.

No game ROMs are included or provided.

The native Vulkan Launcher requires CivHomebrew SDK **1.3.0 or newer** in
the running Launcher. SW2NES borrows that Launcher's UI view and RGBA
framebuffer; it does not bundle or start another renderer. Menus, CRT masks
and the optional stats graph use the native UI snapshot. Older Gameface
Launchers and browser previews retain the original display path. Restart
Civ VII after updating the Launcher/SDK. The 60 fps feature below describes the
original Gameface build; native simulation and presented FPS are measured
separately below.

The view attachment and cleanup follow
[Moonlight's native UI adapter](https://github.com/XITRIX/Moonlight-CivSwitch/blob/main/app/src/ui/native-ui.js).
NES frames use the SDK's RGBA framebuffer on that same view; they do not
need Moonlight's decoder/video pipelines or a separate display surface.

## Features

- Locked 60 fps, display-synchronised presentation and integer scaling
- Two players (a second controller is detected automatically)
- 4 save-state slots per game, optional auto-load of the latest state, battery saves
- Per-game or global settings
- Aspect ratio (1:1, 8:7, 4:3, Stretch), Integer/Fit scaling, overscan crop
- CRT filters aligned to the pixel grid: scanlines (soft/normal/bold), aperture grille, scanlines + grille, LCD matrix, screen door
- Colour palettes generated from an NTSC composite decode (Default, Vivid, Natural, Warm, Soft, Greyscale)
- Face button remapping with turbo (10/15/20/30 Hz)
- Game Genie codes (on-screen editor, or a `.gg` text file next to the ROM)
- Volume and audio low-pass filter
- Favourites and recently played lists
- Stats overlay with frame-time graph

## Installing

1. In CivHomebrew, enable the FTP server.
2. Upload `sw2nes.civapp` (from a release, or built yourself) into the `Install civapp` folder. CivHomebrew installs it automatically.
3. Upload your `.nes` files into `Root/nes`.
4. Launch **SW2NES** from CivHomebrew.

Game Genie codes can also be uploaded as `Root/nes/<game name>.gg`, one code per
line with an optional description, for example `SXIOPO Infinite lives`.

## Controls

| Switch 2 | NES / app |
| --- | --- |
| D-pad / left stick | D-pad |
| A, B | NES A (default) |
| X, Y | NES B (default) |
| + / - | Start / Select |
| L + R | In-game menu (press again to close) |

All face buttons can be remapped in Settings, including turbo.

## Building

Requirements: [Emscripten](https://emscripten.org/) 3.1.74 (via emsdk), Python 3 and Node.js.

```sh
# Emulator runtime (QuickNES + bridge, compiled to plain JavaScript)
EMSDK=/path/to/emsdk ALIGN_LOWERING=1 tools/build.sh
# Package the app
python3 tools/package_payload.py --directory . --non-interactive --release 1.0.0 -o dist/sw2nes.civapp
```

`tools/build.sh` regenerates `native/cpu_lib.js` (the JavaScript 6502 core) from
`tools/gen_cpu.py`. Build options are described at the top of the script.

## Layout

| Path | Contents |
| --- | --- |
| `app/sw2nes.js` | The app: game list, settings, menus, game loop, video/audio output |
| `app/native.js` | Console access through CivHomebrew: files, controllers, live video texture, AudioOut |
| `native/bridge.cpp` | C interface between QuickNES and the app (frames, audio, states, palettes) |
| `native/*_lib.js` | Hand-written JavaScript for the 6502 CPU, pixel conversion and background rendering |
| `tools/` | Build, packaging, CPU generator, icon generator |
| `bench/` | Tests and benchmarks: browser previews (`preview*.html`), console mock, differential tests |
| `third_party/QuickNES_Core` | QuickNES with SW2NES patches (marked `SW2NES_`) |

## Testing

```sh
# Reference build (C CPU) and differential test: the JavaScript core must match
EMSDK=/path/to/emsdk ALIGN_LOWERING=1 JS_CPU=0 OUT_NAME=ref.js tools/build.sh
node bench/diff.cjs dist/runtime/ref.js dist/runtime/quicknes.js 20000 path/to/roms/*.nes

# UI tests in headless Chrome (browser preview and console mock)
node bench/ui-test.mjs /tmp/shots preview.html
node bench/ui-test.mjs /tmp/shots preview-native.html

# Native UI integration (SDK release extracted separately; no game ROM needed)
node bench/native-ui-test.cjs /path/to/extracted/civhomebrew-sdk-1.3.0
```

The browser previews need a ROM at `bench/test.nes`, which is not included.

Native checks on Switch 2 with SDK 1.3.0, using an existing Contra Force (USA)
ROM, compared 7-second windows after warm-up on the same renderer. No ROMs
were added to the repository. These are simulation/presentation counters,
not an optical refresh measurement or a guarantee for other games.

| Native renderer | Emulated FPS before → after | Presented FPS before → after |
| --- | --- | --- |
| CRT on, stats off | 35.6 → 59.4 | 35.6 → 34.3 |
| CRT on, full stats graph | 5.8 → 59.7 | 5.8 → 29.6 |

Retained UI scenes avoid per-frame DOM serialization and a redundant UI check.
The graph reuses one 480×40 RGBA image per native session. The existing adaptive
frame skip keeps simulation near NES speed; this does not establish 60 rendered
FPS. The native menu uses a bounded row window so its final action is visible
without depending on Gameface scrolling. Browser tests cover Quit at 540p,
720p and 1080p with scrolling disabled, retained graph reuse and UI updates
without DOM serialization. A console check selected the visible Quit action and
returned to the game list on the same native owner.

UI navigation normalizes both stick axes to the D-pad directions, including
left/right repeat. The bottom actions move horizontally; Up returns to the list
or tabs, and Down stays on the action row. Native focus uses a filled accent
background, a light border and bold text, with `data-selected` allowing the SDK
to wait for settled focus styles. Console checks covered Settings/Refresh/Exit,
tab navigation, Exit confirmation/close and the actual Exit highlight pixels.

## Licence

SW2NES is free software under the GNU General Public License v2 (see `LICENSE`),
the same licence as QuickNES (by Shay Green / blargg; libretro QuickNES_Core).
