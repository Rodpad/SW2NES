// Nestopia UE core for the native (hbmenu) build: the same C interface as the
// QuickNES bridge (bridge.cpp), with the prefix nst_. Video comes out as NES
// colour numbers (a custom palette encodes each one), so the front end's own
// palettes, NTSC signal and shaders work with both cores.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <new>
#include <sstream>
#include <string>
#include <vector>

#include "core/api/NstApiEmulator.hpp"
#include "core/api/NstApiMachine.hpp"
#include "core/api/NstApiCartridge.hpp"
#include "core/api/NstApiVideo.hpp"
#include "core/api/NstApiSound.hpp"
#include "core/api/NstApiInput.hpp"
#include "core/api/NstApiCheats.hpp"
#include "core/api/NstApiUser.hpp"
#include "../third_party/nestopia/libretro/nstdatabase.hpp"

using namespace Nes;

#define EXPORT extern "C" __attribute__((used, visibility("default")))

enum { FULL_W = 256, FULL_H = 240, CROP = 8, OUT_H = FULL_H - 2 * CROP, SAMPLE_RATE = 48000, MAX_SAMPLES = 4096, AUDIO_CAP = 8192 };

struct Instance {
  Api::Emulator emulator;
  Api::Machine* machine = 0;
  Api::Input::Controllers input;
};
static Instance* inst = 0;   // the real game
static Instance* ahead = 0;  // run-ahead instance (same ROM, state copied in each frame)
static std::string rom_data;
static uint8_t* rom = 0;
static long rom_size = 0;

static uint32_t frame_raw[FULL_W * FULL_H];      // palette-encoded colour numbers (see setupVideo)
static uint16_t colors[FULL_W * OUT_H];           // NES colour numbers of the shown picture
static uint32_t rgba[FULL_W * OUT_H];             // converted picture
static uint8_t base_palette[512 * 3];             // Nestopia's own palette (Default)
static uint8_t custom_pal[512 * 3];               // front end's palette (when on)
static bool custom_pal_on = false, have_frame = false;
static uint32_t pal32[512];

static int16_t mono[MAX_SAMPLES], stereo[AUDIO_CAP * 2];
static int rs_pos = 0, rs_step = 0x10000;
static int16_t rs_last = 0;
static int audio_vol = 256, audio_lp = 0, lp_state = 0, audio_fill = 0, audio_discard = 0;

static uint8_t* sram = 0;
static unsigned long sram_size = 0;

static std::string state_buf;
static std::vector<std::string> snaps;
struct RawCode { int addr, value, compare; };
static std::vector<RawCode> cheat_list;  // also given to a new run-ahead instance
static int applyCodes(Instance* in);

/* ---------------- Battery RAM (file callbacks) ---------------- */
// Nestopia asks for battery RAM through a file callback while the cartridge
// loads; GetRawStorage hands out the cartridge's own memory, which the front
// end then fills from its .srm file and hashes to detect changes.
static void NST_CALLBACK fileIo(void*, Api::User::File& file) {
  switch (file.GetAction()) {
    case Api::User::File::LOAD_BATTERY:
    case Api::User::File::LOAD_EEPROM: {
      void* p = 0; ulong n = 0;
      file.GetRawStorage(p, n);
      if (p && n && !sram) { sram = (uint8_t*)p; sram_size = n; }
      break;
    }
    default: break;
  }
}

/* ---------------- Video ---------------- */
static void updatePal32() {
  const uint8_t* src = custom_pal_on ? custom_pal : base_palette;
  for (int i = 0; i < 512; i++) pal32[i] = 0xff000000u | ((uint32_t)src[i * 3 + 2] << 16) | ((uint32_t)src[i * 3 + 1] << 8) | src[i * 3];
}
// Encode colour number i as RGB (i & 255, i >> 8, 0): the 32-bit output then
// carries the colour number exactly (no filter, neutral picture settings).
static bool setupVideo(Instance* in, bool snapshotPalette) {
  Api::Video video(in->emulator);
  video.EnableUnlimSprites(false);
  if (snapshotPalette) {
    video.GetPalette().SetMode(Api::Video::Palette::MODE_YUV);
    Api::Video::Palette::Colors c = video.GetPalette().GetColors();
    if (c) for (int i = 0; i < 512; i++) for (int k = 0; k < 3; k++) base_palette[i * 3 + k] = c[i][k];
  }
  static uint8_t encode[512][3];
  for (int i = 0; i < 512; i++) { encode[i][0] = (uint8_t)(i & 255); encode[i][1] = (uint8_t)(i >> 8); encode[i][2] = 0; }
  video.GetPalette().SetMode(Api::Video::Palette::MODE_CUSTOM);
  if (video.GetPalette().SetCustom(encode, Api::Video::Palette::EXT_PALETTE) != RESULT_OK) return false;
  video.SetBrightness(Api::Video::DEFAULT_BRIGHTNESS);
  video.SetSaturation(Api::Video::DEFAULT_SATURATION);
  video.SetContrast(Api::Video::DEFAULT_CONTRAST);
  video.SetHue(Api::Video::DEFAULT_HUE);
  Api::Video::RenderState rs;
  rs.filter = Api::Video::RenderState::FILTER_NONE;
  rs.width = FULL_W; rs.height = FULL_H;
  rs.bits.count = 32;
  rs.bits.mask.r = 0x00ff0000; rs.bits.mask.g = 0x0000ff00; rs.bits.mask.b = 0x000000ff;
  return video.SetRenderState(rs) == RESULT_OK;
}
static void convert() {
  for (int y = 0; y < OUT_H; y++) {
    const uint32_t* in = frame_raw + (y + CROP) * FULL_W;
    uint16_t* c = colors + y * FULL_W;
    uint32_t* o = rgba + y * FULL_W;
    for (int x = 0; x < FULL_W; x++) {
      uint32_t v = in[x];
      uint16_t n = (uint16_t)((((v >> 16) & 255) | (((v >> 8) & 1) << 8)) & 511);
      c[x] = n; o[x] = pal32[n];
    }
  }
  have_frame = true;
}

/* ---------------- Audio (same path as the QuickNES bridge) ---------------- */
static void audioFrom(long n) {
  if (audio_discard) return;
  if (audio_vol != 256 || audio_lp) {
    for (long i = 0; i < n; i++) {
      int v = (mono[i] * audio_vol) >> 8;
      if (audio_lp) { lp_state += (v - lp_state) >> audio_lp; v = lp_state; }
      mono[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
  }
  if (audio_fill + n + n / 64 + 4 > AUDIO_CAP) audio_fill = 0;
  int16_t* dst = stereo + audio_fill * 2; int out = 0;
  if (rs_step == 0x10000 && rs_pos == 0) {
    for (long i = 0; i < n; i++) { int16_t v = rs_last; dst[0] = v; dst[1] = v; dst += 2; rs_last = mono[i]; }
    out = (int)n;
  } else {
    for (long i = 0; i < n; i++) {
      int x = mono[i];
      while (rs_pos < 0x10000) {
        int16_t v = (int16_t)(rs_last + (((x - rs_last) * (rs_pos >> 2)) >> 14));
        dst[0] = v; dst[1] = v; dst += 2; out++;
        rs_pos += rs_step;
      }
      rs_pos -= 0x10000; rs_last = (int16_t)x;
    }
  }
  audio_fill += out;
}

/* ---------------- Instances ---------------- */
static Instance* makeInstance(bool primary, const char** err) {
  Instance* in = new (std::nothrow) Instance();
  if (!in) { *err = "Out of memory"; return 0; }
  in->machine = new Api::Machine(in->emulator);
  Api::Cartridge::Database db(in->emulator);
  static std::string dbText;
  if (dbText.empty()) dbText.assign((const char*)nst_db_xml, sizeof(nst_db_xml));
  std::istringstream dbs(dbText);
  db.Load(dbs);
  db.Enable(true);
  std::istringstream rs(rom_data, std::ios::in | std::ios::binary);
  Result r = in->machine->Load(rs, Api::Machine::FAVORED_NES_NTSC);
  if (NES_FAILED(r)) {
    *err = r == RESULT_ERR_UNSUPPORTED_MAPPER ? "unsupported mapper" : r == RESULT_ERR_CORRUPT_FILE ? "corrupt file" :
           r == RESULT_ERR_INVALID_FILE ? "invalid file" : r == RESULT_ERR_OUT_OF_MEMORY ? "out of memory" : "load failed";
    delete in->machine; delete in; return 0;
  }
  Api::Machine(in->emulator).SetMode(Api::Machine::NTSC);
  if (!setupVideo(in, primary)) { *err = "video setup failed"; delete in->machine; delete in; return 0; }
  Api::Sound sound(in->emulator);
  sound.SetSampleRate(SAMPLE_RATE);
  sound.SetVolume(Api::Sound::ALL_CHANNELS, 100);
  Api::Input(in->emulator).ConnectController(0, Api::Input::PAD1);
  Api::Input(in->emulator).ConnectController(1, Api::Input::PAD2);
  return in;
}
static void freeInstance(Instance*& in) {
  if (!in) return;
  in->machine->Unload();
  delete in->machine; delete in; in = 0;
}

/* ---------------- C interface ---------------- */
EXPORT uint8_t* nst_rom_alloc(long size) { free(rom); rom = (uint8_t*)malloc(size); rom_size = rom ? size : 0; return rom; }
EXPORT void nst_unload() {
  freeInstance(ahead);
  freeInstance(inst);
  sram = 0; sram_size = 0; have_frame = false; snaps.clear(); cheat_list.clear();
  audio_fill = 0; rs_pos = 0; rs_step = 0x10000; rs_last = 0; lp_state = 0;
}
EXPORT const char* nst_load() {
  nst_unload();
  if (!rom || rom_size < 16) return "No ROM data";
  rom_data.assign((const char*)rom, rom_size);
  Api::User::fileIoCallback.Set(fileIo, 0);
  const char* err = 0;
  inst = makeInstance(true, &err);
  if (!inst) return err;
  updatePal32();
  // The front end copies its saved battery RAM into nst_sram() now, before power-on.
  return 0;
}
EXPORT void nst_power() { if (inst) inst->machine->Power(true); }
EXPORT void nst_reset(int hard) { if (inst) inst->machine->Reset(hard != 0); }

static int execute(Instance* in, int pad1, int pad2, bool render, bool keepAudio) {
  in->input.pad[0].buttons = pad1; in->input.pad[1].buttons = pad2;
  Api::Video::Output video(frame_raw, FULL_W * 4);
  Api::Sound::Output sound(mono, MAX_SAMPLES);
  sound.length[0] = (SAMPLE_RATE + 59) / 60 + 64;
  sound.length[1] = 0;
  Result r = in->emulator.Execute(render ? &video : 0, &sound, &in->input);
  if (NES_FAILED(r)) return -1;
  if (keepAudio) audioFrom(sound.length[0]);
  return 0;
}
EXPORT int nst_frame(int pad1, int pad2, int render) {
  if (!inst) return -1;
  if (execute(inst, pad1, pad2, render != 0, true) < 0) return -2;
  if (render) convert();
  return 0;
}
EXPORT void nst_convert() {}
EXPORT void nst_reconvert() { updatePal32(); if (have_frame) for (int i = 0; i < FULL_W * OUT_H; i++) rgba[i] = pal32[colors[i]]; }
EXPORT int nst_runahead(int pad1, int pad2, int n) {
  if (!inst || n <= 0) return -1;
  if (!ahead) {
    const char* err = 0;
    ahead = makeInstance(false, &err);
    if (!ahead) return -1;
    ahead->machine->Power(true);
    applyCodes(ahead);
  }
  std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
  if (NES_FAILED(inst->machine->SaveState(ss, Api::Machine::NO_COMPRESSION))) return -1;
  ss.seekg(0);
  if (NES_FAILED(ahead->machine->LoadState(ss))) return -1;
  // Cheats are per emulator: mirror them (set by nst_set_cheats).
  for (int i = 0; i < n; i++) if (execute(ahead, pad1, pad2, i == n - 1, false) < 0) return -1;
  convert();
  return 0;
}
EXPORT void nst_set_audio(int volume, int lowpass) {
  audio_vol = volume < 0 ? 0 : volume > 512 ? 512 : volume;
  audio_lp = lowpass < 0 ? 0 : lowpass > 3 ? 3 : lowpass;
}
EXPORT void nst_set_audio_ratio(double ratio) {
  if (ratio < 0.99) ratio = 0.99; if (ratio > 1.01) ratio = 1.01;
  rs_step = (int)(65536.0 / ratio + 0.5);
  if (rs_step == 0x10000) rs_pos = 0;
}
EXPORT void nst_set_audio_discard(int on) { audio_discard = on != 0; }
EXPORT int nst_audio_take() { int n = audio_fill; audio_fill = 0; return n; }
EXPORT int16_t* nst_audio() { return stereo; }
EXPORT void nst_audio_reset() { audio_fill = 0; rs_pos = 0; rs_last = 0; lp_state = 0; }
EXPORT uint32_t* nst_video() { return rgba; }
EXPORT const uint16_t* nst_color_frame() { return colors; }
EXPORT int nst_width() { return FULL_W; }
EXPORT int nst_height() { return OUT_H; }
EXPORT uint8_t* nst_palette_buffer() { return custom_pal; }
EXPORT void nst_use_palette(int on) { custom_pal_on = on != 0; }
EXPORT int nst_has_battery() { return sram && sram_size ? 1 : 0; }
EXPORT uint8_t* nst_sram() { return sram; }
EXPORT int nst_sram_size() { return (int)sram_size; }

EXPORT long nst_state_save() {
  if (!inst) return 0;
  std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
  if (NES_FAILED(inst->machine->SaveState(ss, Api::Machine::NO_COMPRESSION))) return 0;
  state_buf = ss.str();
  return (long)state_buf.size();
}
EXPORT char* nst_state_buffer(long size) { if ((long)state_buf.size() < size) state_buf.resize(size); return &state_buf[0]; }
EXPORT int nst_state_load(long size) {
  if (!inst || size <= 0 || size > (long)state_buf.size()) return 0;
  std::stringstream ss(std::string(state_buf.data(), size), std::ios::in | std::ios::binary);
  return NES_FAILED(inst->machine->LoadState(ss)) ? 0 : 1;
}
// Rewind snapshots: uncompressed states kept in memory.
EXPORT int nst_snap_alloc(int n) { snaps.assign(n > 0 ? n : 0, std::string()); return (int)snaps.size(); }
EXPORT int nst_snap_size() { for (auto& x : snaps) if (!x.empty()) return (int)x.size(); return 0; }
EXPORT int nst_snap_save(int i) {
  if (!inst || i < 0 || i >= (int)snaps.size()) return 0;
  std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
  if (NES_FAILED(inst->machine->SaveState(ss, Api::Machine::NO_COMPRESSION))) return 0;
  snaps[i] = ss.str();
  return 1;
}
EXPORT int nst_snap_load(int i) {
  if (!inst || i < 0 || i >= (int)snaps.size() || snaps[i].empty()) return 0;
  std::stringstream ss(snaps[i], std::ios::in | std::ios::binary);
  return NES_FAILED(inst->machine->LoadState(ss)) ? 0 : 1;
}
// Game Genie through Nestopia's cheat engine (both instances). Codes arrive
// decoded by the front end (address, value, compare or -1), so every code the
// front end accepts works the same with both cores.
static int applyCodes(Instance* in) {
  Api::Cheats cheats(in->emulator);
  cheats.ClearCodes();
  int ok = 0;
  for (auto& c : cheat_list) {
    Api::Cheats::Code code((ushort)c.addr, (uchar)c.value, (uchar)(c.compare < 0 ? 0 : c.compare), c.compare >= 0);
    if (NES_SUCCEEDED(cheats.SetCode(code))) ok++;
  }
  return ok;
}
EXPORT int nst_set_cheats(const int* addr, const int* value, const int* compare, int n) {
  cheat_list.clear();
  for (int i = 0; i < n; i++) cheat_list.push_back(RawCode{addr[i], value[i], compare[i]});
  if (!inst) return 0;
  int ok = applyCodes(inst);
  if (ahead) applyCodes(ahead);
  return ok;
}
