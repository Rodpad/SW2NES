// SW2NES native front end: audout with a small pool of reusable buffers.
#include "audio.h"

#include <switch.h>
#include <malloc.h>
#include <string.h>

namespace audio {

enum { POOL = 8, CAPACITY_FRAMES = 4096, BYTES = (CAPACITY_FRAMES * 4 + 0xfff) & ~0xfff };
static AudioOutBuffer buffers[POOL];
static bool busy[POOL];
static void* memory[POOL];
static bool ready = false;
static u64 pushedFrames = 0, playedBase = 0;
static bool counterOk = false;
static int counterDiv = 1;
static int16_t silence[CAPACITY_FRAMES * 2];

bool init(std::string& error) {
  Result rc = audoutInitialize();
  if (R_FAILED(rc)) { error = "audout unavailable (" + std::to_string(rc) + ")"; return false; }
  for (int i = 0; i < POOL; i++) {
    memory[i] = memalign(0x1000, BYTES);
    if (!memory[i]) { error = "audio memory"; return false; }
    memset(memory[i], 0, BYTES);
    busy[i] = false;
  }
  rc = audoutStartAudioOut();
  { u64 played = 0; if (R_SUCCEEDED(audoutGetAudioOutPlayedSampleCount(&played))) playedBase = played; }
  if (R_FAILED(rc)) { error = "audout start failed (" + std::to_string(rc) + ")"; audoutExit(); return false; }
  ready = true;
  return true;
}

static void reclaim() {
  for (;;) {
    AudioOutBuffer* released = NULL; u32 count = 0;
    if (R_FAILED(audoutGetReleasedAudioOutBuffer(&released, &count)) || !count || !released) break;
    for (int i = 0; i < POOL; i++) if (&buffers[i] == released) busy[i] = false;
  }
}

void push(const int16_t* stereo, int frames) {
  if (!ready || frames <= 0) return;
  reclaim();
  if (frames > CAPACITY_FRAMES) frames = CAPACITY_FRAMES;
  for (int i = 0; i < POOL; i++) {
    if (busy[i]) continue;
    memcpy(memory[i], stereo, frames * 4);
    AudioOutBuffer& b = buffers[i];
    memset(&b, 0, sizeof b);
    b.buffer = memory[i];
    b.buffer_size = BYTES;
    b.data_size = frames * 4;
    b.data_offset = 0;
    if (R_SUCCEEDED(audoutAppendAudioOutBuffer(&b))) { busy[i] = true; pushedFrames += frames; }
    return;
  }
}

int pending() {
  if (!ready) return 0;
  reclaim();
  int n = 0;
  for (int i = 0; i < POOL; i++) n += busy[i];
  return n;
}

// The played counter's unit is found from its rate over the first half second
// (48000/s = stereo frames, 96000/s = single samples); until then, and if it
// never moves, the queue is estimated from buffer counts.
static u64 rateStartPlayed = 0;
static double rateStartMs = -1;
static double msNow() { return armTicksToNs(armGetSystemTick()) / 1e6; }
double queuedFrames() {
  if (!ready) return 0;
  u64 played = 0;
  if (R_SUCCEEDED(audoutGetAudioOutPlayedSampleCount(&played))) {
    if (!counterOk) {
      double t = msNow();
      if (rateStartMs < 0 && played > 0) { rateStartMs = t; rateStartPlayed = played; }
      else if (rateStartMs >= 0 && t - rateStartMs >= 500) {
        double rate = (played - rateStartPlayed) * 1000.0 / (t - rateStartMs);
        if (rate > 20000) { counterOk = true; counterDiv = rate > 72000 ? 2 : 1; playedBase = played; pushedFrames = (u64)(pending() * 800.0); }
        else rateStartMs = -1;
      }
    }
    if (counterOk) {
      if (played < playedBase) playedBase = played;
      double q = (double)pushedFrames - (double)(played - playedBase) / counterDiv;
      if (q < 0) { playedBase = played; pushedFrames = 0; q = 0; }  // an underrun: played caught up
      return q;
    }
  }
  return pending() * 800.0;
}
void prefill(int frames) {
  while (frames > 0) { int n = frames > 1600 ? 1600 : frames; push(silence, n); frames -= n; }
}
void shutdown() {
  if (!ready) return;
  audoutStopAudioOut();
  bool flushed; audoutFlushAudioOutBuffers(&flushed);
  audoutExit();
  for (int i = 0; i < POOL; i++) free(memory[i]);
  ready = false;
}

}  // namespace audio
