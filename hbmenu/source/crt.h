// CRT overlay strips, ported from app/sw2nes.js (axisMap/rowAlpha/colAlpha):
// per screen row and column darkening placed on the exact screen pixels
// where NES pixel boundaries fall, so patterns stay aligned at any scale.
#pragma once
#include <stdint.h>
#include <math.h>
#include <vector>

namespace crt {

struct Cell { int src, fromStart, toEnd, size; };

inline std::vector<Cell> axisMap(int n, double scale, double offset) {
  std::vector<int> src(n);
  std::vector<Cell> out(n);
  for (int i = 0; i < n; i++) src[i] = (int)floor((i + 0.5) / scale + offset);
  int start = 0;
  for (int i = 0; i < n; i++) {
    if (i == n - 1 || src[i + 1] != src[i]) {
      for (int j = start; j <= i; j++) out[j] = Cell{src[j], j - start, i - j, i - start + 1};
      start = i + 1;
    }
  }
  return out;
}
inline double pos(const Cell& o) { return (o.fromStart + 0.5) / o.size; }
inline double edge(const Cell& o) { return o.size >= 3 ? fabs(pos(o) * 2 - 1) : o.size == 2 ? (o.fromStart ? 1 : 0) : 0.6; }
inline int prof(double d, double strength, double power) { return (int)lround(strength * pow(d, power)); }

inline int rowAlpha(int style, const Cell& r) {
  double d = edge(r);
  switch (style) {
    case 1: return prof(d, 175, 2);
    case 2: return prof(d, 105, 2.4);
    case 3: return prof(d, 235, 1.5);
    case 4: return prof(d, 60, 3);
    case 5: return prof(d, 160, 2);
    case 9: return r.toEnd == 0 ? (r.size >= 3 ? 110 : 80) : 0;
    case 10: return r.toEnd == 0 ? 60 : 0;
  }
  return 0;
}
inline int colAlpha(int style, const Cell& c) {
  double d = edge(c);
  switch (style) {
    case 4: return prof(d, 130, 1.8);
    case 5: return prof(d, 75, 2.2);
    case 9: return c.toEnd == 0 ? (c.size >= 3 ? 110 : 80) : 0;
    case 10: return c.toEnd == 0 ? 60 : 0;
  }
  return 0;
}

// Fills the row strip (picH entries) and column strip (picW entries).
inline void strips(int style, int picW, int picH, double sx, double sy, double ox, double oy,
                   std::vector<uint8_t>& rows, std::vector<uint8_t>& cols) {
  std::vector<Cell> cm = axisMap(picW, sx, ox), rm = axisMap(picH, sy, oy);
  rows.resize(picH); cols.resize(picW);
  for (int y = 0; y < picH; y++) { int a = rowAlpha(style, rm[y]); rows[y] = (uint8_t)(a < 0 ? 0 : a > 255 ? 255 : a); }
  for (int x = 0; x < picW; x++) { int a = colAlpha(style, cm[x]); cols[x] = (uint8_t)(a < 0 ? 0 : a > 255 ? 255 : a); }
}

}  // namespace crt
