// CRT-Geom style (after cgwg's CRT-Geom): Lanczos2 horizontal sampling, beam
// scanlines whose width grows with brightness, magenta/green dot mask, gamma.
const float INPUT_GAMMA = 2.4, OUTPUT_GAMMA = 2.2, MASK = 0.3;
vec3 lineAt(float y, float x) {
  float fx = x - 0.5, i0 = floor(fx), t = fx - i0;
  vec4 d = vec4(1.0 + t, t, 1.0 - t, 2.0 - t), pi = 3.14159265 * max(abs(d), 1e-5);
  vec4 w = 2.0 * sin(pi) * sin(pi / 2.0) / (pi * pi);
  w /= dot(w, vec4(1.0));
  int iy = int(y);
  vec3 c = fetch(ivec2(int(i0) - 1, iy)) * w.x + fetch(ivec2(int(i0), iy)) * w.y + fetch(ivec2(int(i0) + 1, iy)) * w.z + fetch(ivec2(int(i0) + 2, iy)) * w.w;
  return pow(max(c, 0.0), vec3(INPUT_GAMMA));
}
vec3 beamWeight(float dist, vec3 c) {
  vec3 wid = 2.0 + 2.0 * pow(c, vec3(4.0));
  vec3 w = vec3(dist / 0.3);
  return 1.4 * exp(-pow(w * inversesqrt(0.5 * wid), wid)) / (0.6 + 0.2 * wid);
}
vec3 shade(vec2 f, vec2 pw) {
  vec2 p = f * src; float y = p.y - 0.5, iy = floor(y), dy = y - iy;
  vec3 c0 = lineAt(iy, p.x), c1 = lineAt(iy + 1.0, p.x);
  vec3 c = c0 * beamWeight(dy, c0) + c1 * beamWeight(1.0 - dy, c1);
  vec3 mask = mix(vec3(1.0, 1.0 - MASK, 1.0), vec3(1.0 - MASK, 1.0, 1.0 - MASK), mod(floor(pw.x), 2.0));
  return pow(c * mask, vec3(1.0 / OUTPUT_GAMMA));
}
