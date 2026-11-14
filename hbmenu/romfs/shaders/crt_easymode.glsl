// CRT-Easymode style (after EasyMode's shader): sharp horizontal sampling,
// scanline beams that widen on bright pixels, aperture-grille mask, glow.
const float SHARPNESS = 0.5, BEAM_MIN = 0.86, BEAM_MAX = 1.0, SCAN_STRENGTH = 1.0, MASK = 0.3, BOOST = 1.25, GLOW = 0.05;
vec3 sharpRow(float y, float x) {
  float fx = x - 0.5, i = floor(fx), t = fx - i;
  t = clamp((t - 0.5) * (1.0 + SHARPNESS * 4.0) + 0.5, 0.0, 1.0);
  int iy = int(y);
  vec3 a = fetch(ivec2(int(i), iy)), b = fetch(ivec2(int(i) + 1, iy));
  return mix(a, b, t);
}
vec3 shade(vec2 f, vec2 pw) {
  vec2 p = f * src; float y = p.y - 0.5, iy = floor(y), dy = y - iy;
  vec3 c0 = sharpRow(iy, p.x), c1 = sharpRow(iy + 1.0, p.x);
  c0 *= c0; c1 *= c1;  // to linear
  vec3 w0 = mix(vec3(BEAM_MIN), vec3(BEAM_MAX), c0), w1 = mix(vec3(BEAM_MIN), vec3(BEAM_MAX), c1);
  vec3 s0 = exp(-pow(dy / (w0 * 0.45), vec3(2.0)) * SCAN_STRENGTH * 2.0);
  vec3 s1 = exp(-pow((1.0 - dy) / (w1 * 0.45), vec3(2.0)) * SCAN_STRENGTH * 2.0);
  vec3 c = c0 * s0 + c1 * s1;
  vec3 glow = lin(p); glow *= glow;
  float x = mod(floor(pw.x), 3.0);
  vec3 m = vec3(1.0 - MASK); if (x == 0.0) m.r = 1.0; else if (x == 1.0) m.g = 1.0; else m.b = 1.0;
  c = c * m * BOOST + glow * GLOW;
  return sqrt(max(c, 0.0));
}
