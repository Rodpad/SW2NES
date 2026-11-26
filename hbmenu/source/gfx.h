// SW2NES native front end: drawing with OpenGL ES 3 on the default window.
// Coordinates are screen pixels, origin top left, on a fixed 1920x1080 surface.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

namespace gfx {

enum { SCREEN_W = 1920, SCREEN_H = 1080 };
// The interface (rects, text, images, clips) is laid out on a UI_W x UI_H
// canvas and drawn scaled by UI_SCALE (1.25: everything 25% larger). The game
// picture is placed in real screen pixels.
#define UI_SCALE 1.25f
enum { UI_W = 1536, UI_H = 864 };

struct Color { float r, g, b, a; };
Color rgb(uint32_t hex, float alpha = 1.0f);  // 0xRRGGBB

bool init(std::string& error);
void shutdown();
void beginFrame(Color clear);
// Swaps. Frame queue modes (input latency): 0 = driver default, 1 = at most one
// frame in flight (waitPrevious() before reading input), 2 = wait for the GPU
// to finish each frame right after the swap.
void endFrame(int queueMode = 0);
void waitPrevious();
bool screenshot(const char* path);
double gpuMs();             // GPU time per frame (smoothed), -1 if unknown
bool gpuTimerAvailable();  // the frame drawn so far, as PNG
void setHighRefresh(bool on);
bool displayInfo(unsigned& hz, bool& vrr, bool& can120);  // VI2: current refresh, VRR active, 120 Hz supported  // 120 Hz / VRR where the screen allows it; else every 60 Hz refresh  // mode 1: waits for the previous frame's GPU work
extern double swapMs, waitMs;  // last frame: time in eglSwapBuffers / in GPU waits

// Filled rounded rectangle with an optional border drawn inside its edge.
void rect(float x, float y, float w, float h, Color fill, float radius = 0, float border = 0, Color borderColor = Color{0, 0, 0, 0});
void clip(int x, int y, int w, int h);  // scissor; w<=0 clears
void noClip();

// Fonts: 0 = regular, 1 = bold, 2 = monospace.
enum Font { REGULAR = 0, BOLD = 1, MONO = 2 };
float textWidth(const std::string& s, float size, Font f = REGULAR);
// Draws one line with its top at y (line box of 1.25 * size); returns its width.
float text(const std::string& s, float x, float y, float size, Color c, Font f = REGULAR);
// Fits text into width with "...", for list rows.
std::string ellipsize(const std::string& s, float width, float size, Font f = REGULAR);

// Game picture: RGBA texture of w x h shown at (x, y) as dw x dh (nearest),
// cropped to the source rectangle (sx, sy, sw, sh) in texels.
struct Picture;
Picture* pictureCreate(int w, int h);
void pictureUpload(Picture* p, const uint32_t* rgba);
void pictureDraw(Picture* p, int x, int y, int dw, int dh, int sx, int sy, int sw, int sh, float dim = 1.0f);
// CRT overlay strips: per screen row / column darkening (0..255), length dh / dw.
void pictureSetCrt(Picture* p, const uint8_t* rowAlpha, int rows, const uint8_t* colAlpha, int cols);
// Rounded corners (radius and edge softness in screen px; radius 0 = off),
// vignette (darkening at the corners 0..1, starting at 0..1 from the centre)
// and barrel curvature (0 = flat).
void pictureSetEffects(Picture* p, float cornerRadius, float cornerSoftness, float vignetteStrength, float vignetteStart, float curvature);
void pictureDestroy(Picture* p);
// Picture shaders: files in romfs:/shaders (each compiled with prelude.glsl).
// Returns how many compiled; failures are described in log. Index 0 must be
// the plain one ("none"); a shader that failed falls back to it.
int loadShaders(const std::vector<std::string>& files, std::string& log);
void pictureSetShader(Picture* p, int shader);
void pictureSetBoost(Picture* p, float exponent);            // 1 = off
void pictureSetBeam(Picture* p, int subframes, int current); // CRT beam simulation; 1 subframe = off
// NTSC signal: 0 RGB, 1 composite, 2 composite with dot crawl, 3 S-Video.
// decode = hue, saturation, brightness, contrast (as the palettes).
void pictureSetSignal(Picture* p, int signal, const float decode[4], float gammaOut);
void pictureUploadColors(Picture* p, const uint16_t* colors, float emuFrame);  // NES colour number per pixel
int shaderCount();
bool shaderOk(int i);

// A small RGBA image (graph, icons), updated whole.
struct Image;
Image* imageCreate(int w, int h);
void imageUpload(Image* im, const uint32_t* rgba);
void imageDraw(Image* im, float x, float y, float w, float h, float alpha = 1.0f);
void imageDestroy(Image* im);
// A smooth-scaled image (mipmapped), for art shown at any size.
Image* imageCreateSmooth(int w, int h, const uint32_t* rgba);
int imageWidth(Image* im);
int imageHeight(Image* im);

// A box in 3D: a cuboid with `front` on its front face (frontW x frontH UI units, depth deep), seen
// in perspective with its centre at (cx, cy), turned yawDeg around the vertical and pitchDeg
// around the horizontal. The other faces are `side`; all faces are lit from the upper left.
// front may be null (all faces `side`).
// back and spine (both long sides) may be null: those faces are `side` then. A spine image lying on its
// side (wider than tall) is turned to run up the box.
void box3D(Image* front, Image* back, Image* spine, float cx, float cy, float frontW, float frontH, float depth, float yawDeg, float pitchDeg, Color side);

}  // namespace gfx
