#version 300 es
precision highp float;
precision highp usampler2D;
// NTSC composite decoder (method after the NESdev wiki / Bisqwit's NTSC
// filter). Rebuilds the NES's composite signal from each pixel's colour
// number (8 samples per pixel, 12 per colour-subcarrier cycle, phase moving
// 4 samples per scanline), then decodes it like a TV: luma averaged over one
// subcarrier cycle, chroma (I/Q) demodulated over `chromaWin` samples. Output
// is 3 columns per NES pixel.
uniform usampler2D colors;  // R16UI: NES colour 0-511 (incl. emphasis) per pixel
uniform vec2 inSize;        // NES pixels (W, H)
uniform float frameCount;
uniform int dotCrawl;       // 1: subcarrier phase advances each frame
uniform float lumaWin;      // samples: 12 composite, 6 S-Video
uniform float chromaWin;    // samples: 24 composite, 18 S-Video
uniform int svideo;         // 1: luma unaffected by chroma (no cross-colour)
uniform vec4 decode;        // hue, saturation, brightness, contrast
uniform float gammaOut;
in vec2 vpos;
out vec4 fragColor;

const float BLACK = 0.518, WHITE = 1.962, ATT = 0.746;
float levelLo(int l) { return l == 0 ? 0.350 : l == 1 ? 0.518 : l == 2 ? 0.962 : 1.550; }
float levelHi(int l) { return l == 0 ? 1.094 : l == 1 ? 1.506 : 1.962; }
bool inPhase(int c, int ph) { return (c + ph) % 12 < 6; }
// Composite level of NES colour `col` at subcarrier phase `ph` (0-11).
float signalAt(int col, int ph, out float lumaOnly) {
  int color = col & 15, level = (col >> 4) & 3, emph = col >> 6;
  if (color > 13) level = 1;
  float lo = levelLo(level), hi = levelHi(level);
  if (color == 0) lo = hi;
  if (color > 12) hi = lo;
  float s = inPhase(color, ph) ? hi : lo;
  bool att = ((emph & 1) != 0 && inPhase(0, ph)) || ((emph & 2) != 0 && inPhase(4, ph)) || ((emph & 4) != 0 && inPhase(8, ph));
  float a = att ? ATT : 1.0;
  lumaOnly = ((hi + lo) * 0.5 * a - BLACK) / (WHITE - BLACK);
  return (s * a - BLACK) / (WHITE - BLACK);
}
void main() {
  // vpos: output texel centre in the (3W x H) target, measured from the top;
  // framebuffer rows count from the bottom, so line y lands in texel row y
  // (the row the picture shaders read as line y) when flipped here.
  int y = int(inSize.y) - 1 - int(vpos.y);
  float centre = vpos.x / 3.0 * 8.0;  // in signal samples
  int linePhase = (y * 4 + (dotCrawl == 1 ? int(frameCount) * 4 : 0)) % 12;
  float Y = 0.0, I = 0.0, Q = 0.0, ny = 0.0, nc = 0.0;
  int s0 = int(floor(centre - chromaWin * 0.5)), s1 = int(floor(centre + chromaWin * 0.5));
  for (int s = s0; s < s1; s++) {
    int px = clamp(s >> 3, 0, int(inSize.x) - 1);
    int col = int(texelFetch(colors, ivec2(px, y), 0).r);
    int ph = ((s % 12) + 12 + linePhase) % 12;
    float lumaOnly, v = signalAt(col, ph, lumaOnly);
    float d = float(s) + 0.5 - centre;
    if (abs(d) <= lumaWin * 0.5) { Y += svideo == 1 ? lumaOnly : v; ny += 1.0; }
    float ang = 3.14159265 * (float(ph) + decode.x) / 6.0;
    I += v * cos(ang); Q += v * sin(ang); nc += 1.0;
  }
  Y = Y / max(ny, 1.0) * decode.w + decode.z;
  I = I / max(nc, 1.0) * decode.y;
  Q = Q / max(nc, 1.0) * decode.y;
  vec3 rgb = vec3(Y + 0.946882 * I + 0.623557 * Q, Y - 0.274788 * I - 0.635691 * Q, Y - 1.108545 * I + 1.709007 * Q);
  fragColor = vec4(pow(clamp(rgb, 0.0, 1.0), vec3(gammaOut)), 1.0);
}
