// CRT-Lottes style (after Timothy Lottes' public-domain CRT shader):
// gaussian horizontal blur, hard gaussian scanlines, aperture-grille mask.
const float HARD_SCAN = -8.0, HARD_PIX = -3.0, MASK_DARK = 0.5, MASK_LIGHT = 1.5, BRIGHT = 1.0;
vec3 toLin(vec3 c) { return c * c; }
float gaus(float pos, float scale) { return exp2(scale * pos * pos); }
vec3 horz(vec2 p, float off, int taps) {
  float y = floor(p.y) + off, dst = fract(p.x) - 0.5; int x = int(floor(p.x));
  vec3 sum = vec3(0.0); float wsum = 0.0;
  for (int k = -2; k <= 2; k++) {
    if (abs(k) > taps / 2) continue;
    float w = gaus(dst - float(k), HARD_PIX);
    sum += toLin(fetch(ivec2(x + k, int(y)))) * w; wsum += w;
  }
  return sum / wsum;
}
float scan(vec2 p, float off) { return gaus(fract(p.y) - 0.5 - off, HARD_SCAN); }
vec3 mask(vec2 pw) {
  float x = fract(floor(pw.x) / 3.0); vec3 m = vec3(MASK_DARK);
  if (x < 0.333) m.r = MASK_LIGHT; else if (x < 0.666) m.g = MASK_LIGHT; else m.b = MASK_LIGHT;
  return m;
}
vec3 shade(vec2 f, vec2 pw) {
  vec2 p = f * src;
  vec3 c = horz(p, -1.0, 3) * scan(p, -1.0) + horz(p, 0.0, 5) * scan(p, 0.0) + horz(p, 1.0, 3) * scan(p, 1.0);
  return sqrt(max(c * mask(pw) * BRIGHT, 0.0));
}
