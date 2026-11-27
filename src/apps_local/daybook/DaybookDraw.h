#pragma once

// A rectangle filled black or white with small rounded corners, for the month's
// today cell, the filled buttons and the sleep-screen panel. Integer only: the
// corner rows are the circle's, found by counting.

#include <GfxRenderer.h>

namespace daybook {

inline void fillRoundRect(GfxRenderer& renderer, const int x, const int y, const int w, const int h, int r,
                          const bool black) {
  if (r > w / 2) r = w / 2;
  if (r > h / 2) r = h / 2;
  if (r < 0) r = 0;
  renderer.fillRect(x, y + r, w, h - 2 * r, black);
  for (int i = 0; i < r; ++i) {
    // Row i from the top sits (r - i - 1/2) above the circle's centre line;
    // scaled by 2 to stay in integers.
    const int dy2 = 2 * (r - i) - 1;
    const int chord2 = 4 * r * r - dy2 * dy2;  // (2 * half chord)^2
    int half = 0;
    while (4 * (half + 1) * (half + 1) <= chord2) ++half;
    const int inset = r - half;
    renderer.fillRect(x + inset, y + i, w - 2 * inset, 1, black);
    renderer.fillRect(x + inset, y + h - 1 - i, w - 2 * inset, 1, black);
  }
}

}  // namespace daybook
