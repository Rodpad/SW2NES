// LCD grid (after "lcd3x"): each NES pixel drawn as red, green and blue
// sub-pixel stripes with thin dark gaps, like a handheld LCD close up.
const float GAP = 0.25, STRIPE = 0.35, BRIGHT = 1.35;
vec3 shade(vec2 f, vec2 pw) {
  vec2 p = f * nesSrc, s = fract(p);
  vec3 c = srcAt(floor(p) + 0.5);
  float third = s.x * 3.0;
  vec3 sub = vec3(1.0 - STRIPE);
  if (third < 1.0) sub.r = 1.0; else if (third < 2.0) sub.g = 1.0; else sub.b = 1.0;
  float scale = min(size.x / nesSrc.x, size.y / nesSrc.y);
  float edge = 1.0 / max(scale, 1.0);
  float gx = smoothstep(0.0, edge * 1.5, s.x) * smoothstep(0.0, edge * 1.5, 1.0 - s.x);
  float gy = smoothstep(0.0, edge * 1.5, s.y) * smoothstep(0.0, edge * 1.5, 1.0 - s.y);
  float gap = mix(1.0 - GAP, 1.0, gx * gy);
  return c * sub * gap * BRIGHT;
}
