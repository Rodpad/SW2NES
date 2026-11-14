// Emulation cores: QuickNES (native/bridge.cpp, nes_*) and Nestopia UE
// (native/bridge_nestopia.cpp, nst_*), behind one table of functions.
#pragma once
#include <stdint.h>

extern "C" {
// QuickNES bridge (also used directly by the host tests)
uint8_t* nes_rom_alloc(long size);
void nes_unload();
const char* nes_load();
void nes_reset(int hard);
void nes_reconvert();
void nes_convert();
int nes_frame(int pad1, int pad2, int render);
void nes_set_audio(int volume, int lowpass);
void nes_set_audio_ratio(double ratio);
int nes_audio_take();
int16_t* nes_audio();
uint32_t* nes_video();
int nes_width();
int nes_height();
int nes_sample_rate();
uint8_t* nes_palette_buffer();
void nes_use_palette(int on);
uint8_t* nes_prg();
long nes_prg_size();
int nes_has_battery();
uint8_t* nes_sram();
uint8_t* nes_ram();
int nes_sram_size();
long nes_state_save();
char* nes_state_buffer(long size);
int nes_state_load(long size);
int nes_snap_alloc(int n);
int nes_snap_size();
int nes_snap_save(int i);
int nes_snap_load(int i);
void nes_set_audio_discard(int on);
const uint8_t* nes_indices();
int nes_index_pitch();
const uint16_t* nes_index_colors();
int nes_runahead(int pad1, int pad2, int ahead);
void nes_audio_reset();
const uint16_t* nes_color_frame();
void nes_power();
}

struct CoreApi {
  const char* name;
  const char* stateTag;  // save-state file tag ("" for QuickNES, ".nst" for Nestopia)
  uint8_t* (*rom_alloc)(long);
  void (*unload)();
  const char* (*load)();
  void (*power)();  // after battery RAM is filled in
  void (*reset)(int);
  void (*reconvert)();
  int (*frame)(int, int, int);
  void (*set_audio)(int, int);
  void (*set_audio_ratio)(double);
  int (*audio_take)();
  int16_t* (*audio)();
  void (*audio_reset)();
  void (*set_audio_discard)(int);
  uint32_t* (*video)();
  const uint16_t* (*color_frame)();  // NES colour numbers of the shown picture
  int (*width)();
  int (*height)();
  uint8_t* (*palette_buffer)();
  void (*use_palette)(int);
  uint8_t* (*prg)();  // PRG ROM for Game Genie patching (QuickNES)
  long (*prg_size)();
  int (*set_cheats)(const int* addr, const int* value, const int* compare, int n);  // core's own cheat engine (Nestopia)
  int (*has_battery)();
  uint8_t* (*sram)();
  int (*sram_size)();
  long (*state_save)();
  char* (*state_buffer)(long);
  int (*state_load)(long);
  int (*snap_alloc)(int);
  int (*snap_size)();
  int (*snap_save)(int);
  int (*snap_load)(int);
  int (*runahead)(int, int, int);
};
extern const CoreApi QUICKNES_CORE, NESTOPIA_CORE;
