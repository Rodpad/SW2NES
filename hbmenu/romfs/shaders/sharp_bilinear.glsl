// Sharp bilinear (after "sharp-bilinear-simple"): crisp pixels, but edges
// between them are blended over less than one output pixel, so pixels look
// even at non-integer scales and with curvature.
vec3 shade(vec2 f, vec2 pw) {
  vec2 scale = size / src, texel = f * src, base = floor(texel), s = fract(texel);
  vec2 range = max(vec2(0.0), 0.5 - 0.5 / scale), d = s - 0.5;
  vec2 g = (d - clamp(d, -range, range)) * scale + 0.5;
  return lin(base + g);
}
