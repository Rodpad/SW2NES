# SW2NES for CivHomebrew (legacy)

This is the former version of SW2NES that ran inside the CivHomebrew launcher
(Civ VII's browser engine, JavaScript only). Its last release was **1.1.1**.
It is no longer maintained: SW2NES now runs natively from hbmenu under kaiHEN
(see the main README).

Kept for reference:

- `app/` - the app (`sw2nes.js`), console bridge (`native.js`), the vendored
  CivHomebrew SDK runtime
- `native/*_lib.js` - hand-written JavaScript 6502 CPU, pixel conversion and
  background renderer used by the wasm2js build
- `tools/` - build and packaging scripts; `bench/` - tests and previews

The build scripts expect the original repository layout (they were written
for the repository root), so paths may need adjusting to build this again.
