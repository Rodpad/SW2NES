// CRT-Aperture style (after EasyMode's crt-aperture): sharp pixels, soft
// scanlines, a strong aperture-grille mask and halation glow.
const float SCAN_LOW = 0.5, SCAN_HIGH = 0.85, MASK = 0.55, GLOW = 0.15, BRIGHT = 1.45;
vec3 shade(vec2 f, vec2 pw) {
  vec2 scale = size / src, texel = f * src, base = floor(texel), s = fract(texel);
  vec2 range = max(vec2(0.0), 0.5 - 0.5 / scale), d = s - 0.5;
  vec3 c = lin(base + (d - clamp(d, -range, range)) * scale + 0.5);
  c *= c;
  float l = luma(c), dy = abs(s.y - 0.5) * 2.0;
  float scanW = mix(SCAN_LOW, SCAN_HIGH, sqrt(l));
  c *= exp(-pow(dy / scanW, 2.0) * 1.5);
  vec3 g = lin(texel + vec2(-1.5, -0.5)) + lin(texel + vec2(1.5, 0.5)) + lin(texel + vec2(0.5, -1.5)) + lin(texel + vec2(-0.5, 1.5));
  g *= 0.25; g *= g;
  float x = mod(floor(pw.x), 3.0);
  vec3 m = vec3(1.0 - MASK); if (x == 0.0) m.r = 1.0; else if (x == 1.0) m.g = 1.0; else m.b = 1.0;
  c = c * m * BRIGHT + g * GLOW;
  return sqrt(max(c, 0.0));
}
