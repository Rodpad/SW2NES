#version 300 es
precision highp float;
// SW2NES picture shader prelude. A shader file defines
//   vec3 shade(vec2 f, vec2 pw)
// f = position in the picture (0..1, after curvature), pw = the same in output pixels.
// Helpers read the (cropped) source: fetch() = exact texel, lin() = bilinear,
// both in source texel units (texel centres at +0.5).
in vec2 vpos;
uniform sampler2D tex;          // source picture (NES frame or NTSC signal)
uniform sampler2D rows;         // CRT strips (shader "None" only)
uniform sampler2D cols;
uniform vec2 origin;            // output rectangle, screen px
uniform vec2 size;
uniform vec2 texSize;           // whole source texture, texels
uniform vec2 srcOff;            // cropped region in the texture, texels
uniform vec2 src;
uniform int crt;
uniform float dim;
uniform vec2 corner;            // radius px, edge softness px
uniform vec2 vignette;          // strength, start
uniform vec2 curve;             // barrel x, y
uniform float frameCount;
uniform sampler2D rgbTex;       // the NES picture itself (also when tex is the NTSC signal)
uniform vec2 nesOff;            // its cropped region, NES pixels
uniform vec2 nesSrc;
uniform float boost;            // brightness boost exponent (1 = off)
out vec4 fragColor;

vec3 fetch(ivec2 i) { i = clamp(i, ivec2(0), ivec2(src) - 1); return texelFetch(tex, ivec2(srcOff) + i, 0).rgb; }
vec3 fetchAt(vec2 p) { return fetch(ivec2(floor(p))); }
vec3 lin(vec2 p) { p = clamp(p, vec2(0.5), src - 0.5); return texture(tex, (srcOff + p) / texSize).rgb; }
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
// NES-pixel helpers (the picture's own grid, independent of the NTSC signal):
// fetchNes = exact NES colour (for edge tests); srcAt = the current source's
// colour at an NES-pixel position (the signal is 3 columns per pixel).
vec3 fetchNes(ivec2 i) { i = clamp(i, ivec2(0), ivec2(nesSrc) - 1); return texelFetch(rgbTex, ivec2(nesOff) + i, 0).rgb; }
vec3 srcAt(vec2 np) {
  if (src.x == nesSrc.x) return fetch(ivec2(floor(np)));
  return lin(vec2(np.x * src.x / nesSrc.x, floor(np.y) + 0.5));
}

vec3 shade(vec2 f, vec2 pw);

void main() {
  vec2 q = (vpos - origin) / size * 2.0 - 1.0;
  if (curve.x > 0.0) q *= 1.0 + q.yx * q.yx * curve;
  vec2 f = q * 0.5 + 0.5, pw = f * size;
  vec3 c = shade(clamp(f, 0.0, 1.0), pw);
  // Brightness boost (AutoHDR-style lift in SDR): 1-(1-c)^k raises mid-tones
  // and highlights, keeps black at 0 and white at 1 (no clipping).
  if (boost > 1.0) c = 1.0 - pow(max(1.0 - c, 0.0), vec3(boost));
  if (vignette.x > 0.0) {
    float r = length(q) / 1.41421, t = clamp((r - vignette.y) / (1.0 - vignette.y), 0.0, 1.0);
    c *= 1.0 - vignette.x * t * t * (3.0 - 2.0 * t);
  }
  if (corner.x > 0.0 || curve.x > 0.0) {
    // One soft edge all the way round. Corners are squircles (superellipse,
    // n = 4): unlike a circular arc, the curve eases in from the straight edge
    // with no sudden change of curvature, as on a CRT tube. The squircle is
    // drawn larger than the arc radius so it looks about as round.
    float R = corner.x * 1.6;
    vec2 h = size * 0.5, d = abs(pw - h) - h + vec2(R);
    vec2 q4 = max(d, 0.0) / max(R, 1.0);
    float n4 = R * sqrt(sqrt(q4.x * q4.x * q4.x * q4.x + q4.y * q4.y * q4.y * q4.y));
    float e = n4 + min(max(d.x, d.y), 0.0) - R;
    float soft = corner.x > 0.0 ? clamp(corner.y, 1.0, corner.x * 0.6) : 1.0;
    c *= smoothstep(0.0, 1.0, clamp(0.5 - e / soft, 0.0, 1.0));
  }
  fragColor = vec4(clamp(c, 0.0, 1.0) * dim, 1.0);
}
