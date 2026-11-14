// CRT-Royale style (lite; after TroggleMonkey's CRT-Royale): gamma-correct
// gaussian beams whose height grows with brightness, an aperture-grille
// phosphor mask (CRT-Royale's default), halation (light spreading in the
// glass) and brightness compensation for the mask.
const float GAMMA_IN = 2.4, GAMMA_OUT = 2.2;
const float SIGMA_MIN = 0.25, SIGMA_MAX = 0.34;   // beam height in lines, dark -> bright
const float SIGMA_H = 0.40;                       // beam width in pixels
const float MASK_DARK = 0.3, HALATION = 0.02;
vec3 toLin(vec3 c) { return pow(max(c, 0.0), vec3(GAMMA_IN)); }
vec3 lineColor(float y, float x) {
  float fx = x - 0.5, i0 = floor(fx); int iy = int(y);
  vec3 sum = vec3(0.0); float ws = 0.0;
  for (int k = -1; k <= 2; k++) {
    float d = fx - (i0 + float(k)), w = exp(-0.5 * d * d / (SIGMA_H * SIGMA_H));
    sum += toLin(fetch(ivec2(int(i0) + k, iy))) * w; ws += w;
  }
  return sum / ws;
}
// Beam profile, one height for all three colours (no colour fringes), area 1
// per line so average brightness is kept.
float spot(float d, vec3 c) {
  float sigma = mix(SIGMA_MIN, SIGMA_MAX, sqrt(clamp(max(c.r, max(c.g, c.b)), 0.0, 1.0)));
  return exp(-0.5 * d * d / (sigma * sigma)) / (sigma * 2.5066);
}
vec3 shade(vec2 f, vec2 pw) {
  vec2 p = f * src; float y = p.y - 0.5, iy = floor(y), dy = y - iy;
  vec3 a = lineColor(iy - 1.0, p.x), b = lineColor(iy, p.x), c = lineColor(iy + 1.0, p.x), d = lineColor(iy + 2.0, p.x);
  vec3 beams = a * spot(dy + 1.0, a) + b * spot(dy, b) + c * spot(1.0 - dy, c) + d * spot(2.0 - dy, d);
  vec3 halo = vec3(0.0);
  for (int k = 0; k < 8; k++) {
    float ang = float(k) * 0.785398;
    halo += toLin(lin(p + vec2(cos(ang), sin(ang)) * 2.0));
  }
  halo /= 8.0;
  float t = mod(floor(pw.x), 3.0);
  vec3 m = vec3(MASK_DARK); if (t == 0.0) m.r = 1.0; else if (t == 1.0) m.g = 1.0; else m.b = 1.0;
  vec3 col = beams * m * (3.0 / (1.0 + 2.0 * MASK_DARK)) + halo * HALATION;
  return pow(max(col, 0.0), vec3(1.0 / GAMMA_OUT));
}
