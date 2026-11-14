// None: exact pixels (nearest neighbour); the CRT styles apply on top.
// With an NTSC signal (3 columns per pixel) columns are blended horizontally.
vec3 shade(vec2 f, vec2 pw) {
  vec2 p = f * src;
  if (src.x > 600.0) return lin(vec2(p.x, floor(p.y) + 0.5));
  return fetchAt(p);
}
