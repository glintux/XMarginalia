#pragma once

// Scales the Walnut-CGB luminance frame (160x144, 8-bit, 0 = black) to the
// panel and dithers it to the framebuffer's 1 bit per pixel (1 = white) with
// serpentine Floyd-Steinberg error diffusion (ported from Ink-boy, MIT).

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace cgbdither {

constexpr int kSourceWidth = 160;
constexpr int kSourceHeight = 144;
constexpr int kMaxScale = 3;
constexpr int kMaxOutputWidth = kSourceWidth * kMaxScale;
constexpr int kMaxRowBytes = kMaxOutputWidth / 8;

constexpr int kWhiteLevel = 255 * 16;
constexpr int kThreshold = 128 * 16;

inline void ditherRowLeftToRight(const uint8_t* srcRow, const int scale, const int16_t* cur, int16_t* next,
                                 uint8_t* rowBits) {
  int px = 0;
  int carry = 0;
  int p = 0;
  int q = 0;
  uint32_t acc = 0;
  for (int g = 0; g < kSourceWidth; ++g) {
    const int lum = static_cast<int>(srcRow[g]) * 16;
    for (int k = 0; k < scale; ++k, ++px) {
      int adjusted = lum + cur[px] + carry;
      adjusted = adjusted < 0 ? 0 : (adjusted > kWhiteLevel ? kWhiteLevel : adjusted);
      const int white = adjusted >= kThreshold;
      const int error = adjusted - (white ? kWhiteLevel : 0);
      carry = (error * 7) / 16;
      next[px - 1] = static_cast<int16_t>(p + (error * 3) / 16);
      p = q + (error * 5) / 16;
      q = error / 16;
      acc = (acc << 1) | static_cast<uint32_t>(white);
      if ((px & 7) == 7) rowBits[px >> 3] = static_cast<uint8_t>(acc);
    }
  }
  next[px - 1] = static_cast<int16_t>(p);
}

inline void ditherRowRightToLeft(const uint8_t* srcRow, const int scale, const int16_t* cur, int16_t* next,
                                 uint8_t* rowBits) {
  int px = kSourceWidth * scale - 1;
  int carry = 0;
  int p = 0;
  int q = 0;
  uint32_t acc = 0;
  for (int g = kSourceWidth - 1; g >= 0; --g) {
    const int lum = static_cast<int>(srcRow[g]) * 16;
    for (int k = 0; k < scale; ++k, --px) {
      int adjusted = lum + cur[px] + carry;
      adjusted = adjusted < 0 ? 0 : (adjusted > kWhiteLevel ? kWhiteLevel : adjusted);
      const int white = adjusted >= kThreshold;
      const int error = adjusted - (white ? kWhiteLevel : 0);
      carry = (error * 7) / 16;
      next[px + 1] = static_cast<int16_t>(p + (error * 3) / 16);
      p = q + (error * 5) / 16;
      q = error / 16;
      acc = (acc >> 1) | (static_cast<uint32_t>(white) << 7);
      if ((px & 7) == 0) rowBits[px >> 3] = static_cast<uint8_t>(acc);
    }
  }
  next[px + 1] = static_cast<int16_t>(p);
}

inline void writePortraitBand(const uint8_t (&rows)[8][kMaxRowBytes], const int rowBytes, const int band,
                              uint8_t* framebuffer, const int gameX, const int gameY, const int panelBytes,
                              const int panelHeight) {
  uint8_t* column = framebuffer + (gameY >> 3) + band;
  for (int c = 0; c < rowBytes; ++c) {
    const unsigned r0 = rows[0][c], r1 = rows[1][c], r2 = rows[2][c], r3 = rows[3][c];
    const unsigned r4 = rows[4][c], r5 = rows[5][c], r6 = rows[6][c], r7 = rows[7][c];
    for (int j = 0; j < 8; ++j) {
      const int shift = 7 - j;
      const unsigned value = (((r0 >> shift) & 1U) << 7) | (((r1 >> shift) & 1U) << 6) |
                             (((r2 >> shift) & 1U) << 5) | (((r3 >> shift) & 1U) << 4) |
                             (((r4 >> shift) & 1U) << 3) | (((r5 >> shift) & 1U) << 2) |
                             (((r6 >> shift) & 1U) << 1) | ((r7 >> shift) & 1U);
      const int px = c * 8 + j;
      column[static_cast<size_t>(panelHeight - 1 - (gameX + px)) * static_cast<size_t>(panelBytes)] =
          static_cast<uint8_t>(value);
    }
  }
}

inline bool cgbDitherFrame(const uint8_t* source, uint8_t* framebuffer, const int gameX, const int gameY,
                           const int scale, const int panelBytes, const int panelHeight, const bool portrait) {
  if (!source || !framebuffer || scale < 1 || scale > kMaxScale || panelBytes <= 0 || panelHeight <= 0) return false;
  if ((portrait ? gameY : gameX) < 0 || ((portrait ? gameY : gameX) & 7) != 0) return false;

  const int outputWidth = kSourceWidth * scale;
  const int outputHeight = kSourceHeight * scale;
  const int rowBytes = outputWidth / 8;

  int16_t errorA[kMaxOutputWidth + 2] = {};
  int16_t errorB[kMaxOutputWidth + 2] = {};
  int16_t* cur = errorA + 1;
  int16_t* next = errorB + 1;

  uint8_t rows[8][kMaxRowBytes];
  uint8_t* const landscapeRow = rows[0];

  for (int py = 0; py < outputHeight; ++py) {
    const uint8_t* srcRow = source + static_cast<size_t>(py / scale) * kSourceWidth;
    uint8_t* bits = portrait ? rows[py & 7] : landscapeRow;
    if ((py & 1) == 0) {
      ditherRowLeftToRight(srcRow, scale, cur, next, bits);
    } else {
      ditherRowRightToLeft(srcRow, scale, cur, next, bits);
    }
    std::swap(cur, next);

    if (!portrait) {
      std::memcpy(framebuffer + static_cast<size_t>(gameY + py) * static_cast<size_t>(panelBytes) + (gameX >> 3),
                  bits, static_cast<size_t>(rowBytes));
    } else if ((py & 7) == 7) {
      writePortraitBand(rows, rowBytes, py >> 3, framebuffer, gameX, gameY, panelBytes, panelHeight);
    }
  }
  return true;
}

}  // namespace cgbdither
