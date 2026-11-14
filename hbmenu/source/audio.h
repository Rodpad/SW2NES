// SW2NES native front end: 48 kHz stereo output through audout.
#pragma once
#include <stdint.h>
#include <string>

namespace audio {
bool init(std::string& error);
void shutdown();
// Queues interleaved stereo frames (one buffer per call). Drops them if all
// buffers are busy.
void push(const int16_t* stereo, int frames);
int pending();  // buffers queued and not yet played
// Stereo frames queued and not yet played (from the played-sample counter);
// falls back to buffers * 800 if the counter is unavailable.
double queuedFrames();
void prefill(int frames);  // queues silence (game start / after a gap)
}
