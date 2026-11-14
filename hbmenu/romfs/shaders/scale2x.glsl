// Scale2x (EPX / AdvMAME2x): doubles the resolution by rounding off
// diagonal staircase edges. Edges are found on the NES pixels themselves, so
// it also works with an NTSC signal (the colour then comes from the signal).
vec3 shade(vec2 f, vec2 pw) {
  vec2 p = f * nesSrc; ivec2 i = ivec2(floor(p)); vec2 s = fract(p);
  vec3 P = fetchNes(i), A = fetchNes(i + ivec2(0, -1)), B = fetchNes(i + ivec2(1, 0)), C = fetchNes(i + ivec2(-1, 0)), D = fetchNes(i + ivec2(0, 1));
  bool left = s.x < 0.5, top = s.y < 0.5;
  vec2 c = vec2(i) + 0.5;
  if (top && left && C == A && C != D && A != B) return srcAt(c + vec2(0.0, -1.0));
  if (top && !left && A == B && A != C && B != D) return srcAt(c + vec2(1.0, 0.0));
  if (!top && left && D == C && D != B && C != A) return srcAt(c + vec2(-1.0, 0.0));
  if (!top && !left && B == D && B != A && D != C) return srcAt(c + vec2(0.0, 1.0));
  return srcAt(vec2(p.x, c.y));
}
