// zfast-CRT style (after Greg Hogan's fast CRT shader): soft horizontal
// filtering, brightness-dependent scanlines, light aperture mask.
const float SCAN_DARK = 0.5, MASK_DARK = 0.25, BRIGHT = 1.2;
vec3 shade(vec2 f, vec2 pw) {
  vec2 p = f * src;
  vec2 s = fract(p) - 0.5;
  vec3 c = lin(vec2(p.x, floor(p.y) + 0.5 + s.y * 0.25));
  float l = luma(c);
  float sl = 1.0 - SCAN_DARK * (1.0 - l * 0.5) * smoothstep(0.0, 0.5, abs(s.y) * (1.6 - l * 0.6));
  float m = mod(floor(pw.x), 3.0) == 2.0 ? 1.0 - MASK_DARK : 1.0;
  return c * sl * m * BRIGHT;
}
