// The two emulation cores' function tables (see core.h).
#include "core.h"

extern "C" {
uint8_t* nst_rom_alloc(long);
void nst_unload();
const char* nst_load();
void nst_power();
void nst_reset(int);
void nst_reconvert();
int nst_frame(int, int, int);
void nst_set_audio(int, int);
void nst_set_audio_ratio(double);
int nst_audio_take();
int16_t* nst_audio();
void nst_audio_reset();
void nst_set_audio_discard(int);
uint32_t* nst_video();
const uint16_t* nst_color_frame();
int nst_width();
int nst_height();
uint8_t* nst_palette_buffer();
void nst_use_palette(int);
int nst_set_cheats(const int*, const int*, const int*, int);
int nst_has_battery();
uint8_t* nst_sram();
int nst_sram_size();
long nst_state_save();
char* nst_state_buffer(long);
int nst_state_load(long);
int nst_snap_alloc(int);
int nst_snap_size();
int nst_snap_save(int);
int nst_snap_load(int);
int nst_runahead(int, int, int);
}

static uint8_t* noPrg() { return 0; }
static long noPrgSize() { return 0; }

const CoreApi QUICKNES_CORE = {
    "QuickNES", "",
    nes_rom_alloc, nes_unload, nes_load, nes_power, nes_reset, nes_reconvert, nes_frame,
    nes_set_audio, nes_set_audio_ratio, nes_audio_take, nes_audio, nes_audio_reset, nes_set_audio_discard,
    nes_video, nes_color_frame, nes_width, nes_height, nes_palette_buffer, nes_use_palette,
    nes_prg, nes_prg_size, nullptr,
    nes_has_battery, nes_sram, nes_sram_size, nes_state_save, nes_state_buffer, nes_state_load,
    nes_snap_alloc, nes_snap_size, nes_snap_save, nes_snap_load, nes_runahead,
};

const CoreApi NESTOPIA_CORE = {
    "Nestopia", ".nst",
    nst_rom_alloc, nst_unload, nst_load, nst_power, nst_reset, nst_reconvert, nst_frame,
    nst_set_audio, nst_set_audio_ratio, nst_audio_take, nst_audio, nst_audio_reset, nst_set_audio_discard,
    nst_video, nst_color_frame, nst_width, nst_height, nst_palette_buffer, nst_use_palette,
    noPrg, noPrgSize, nst_set_cheats,
    nst_has_battery, nst_sram, nst_sram_size, nst_state_save, nst_state_buffer, nst_state_load,
    nst_snap_alloc, nst_snap_size, nst_snap_save, nst_snap_load, nst_runahead,
};
