// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
// imgscale.cpp — bilinear RGB565+A8 scaling, because lv_img's zoom cannot be
// trusted with position.
//
// Twice now the transform pipeline has burned this project. The bloom sprite,
// zoomed UP with SIZE_MODE_REAL and a (0,0) pivot, drew well right of its own
// position; the team logos, zoomed DOWN the same way, drew clipped — a
// calibration spike (mock 12) measured a solid 48px square rendering 17px
// where 26 was requested, cropped at the box edge, with the law changing
// between upscale and downscale AND with the src/zoom call order. The logos
// looked merely "down and to the right" on the panel because their artwork
// insets hid the crop.
//
// The fix both times is the same: produce the pixels we actually want and
// hand LVGL an untransformed image. This is the one scaler, shared by the
// firmware and the desktop harness (the Makefile globs ui/*.cpp), so the two
// can never disagree about what a scaled logo looks like.
#include "imgscale.h"

void imgScaleRgb565A8(const uint8_t* src, int sw, int sh,
                      uint8_t* dst, int dw, int dh) {
  // Fixed-point bilinear. 48->26 class ratios: quality is indistinguishable
  // from lv_img's own filtering at these sizes, minus the geometry bugs.
  for (int y = 0; y < dh; y++) {
    // Map the destination pixel centre back into source space.
    const int32_t fy = ((int32_t)y * 2 + 1) * sh * 128 / dh - 128;  // <<8
    int32_t y0 = fy >> 8;
    int32_t wy = fy & 0xFF;
    if (y0 < 0) { y0 = 0; wy = 0; }
    if (y0 >= sh - 1) { y0 = sh - 2; wy = 255; }

    for (int x = 0; x < dw; x++) {
      const int32_t fx = ((int32_t)x * 2 + 1) * sw * 128 / dw - 128;
      int32_t x0 = fx >> 8;
      int32_t wx = fx & 0xFF;
      if (x0 < 0) { x0 = 0; wx = 0; }
      if (x0 >= sw - 1) { x0 = sw - 2; wx = 255; }

      // The four taps, unpacked to 8-bit.
      int r = 0, g = 0, b = 0, a = 0;
      for (int j = 0; j < 2; j++) {
        for (int i = 0; i < 2; i++) {
          const uint8_t* p = src + ((y0 + j) * sw + (x0 + i)) * 3;
          const uint16_t v = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
          const int w = ((i ? wx : 256 - wx) * (j ? wy : 256 - wy)) >> 8;
          r += (int)(((v >> 11) & 0x1F) * 255 / 31) * w;
          g += (int)(((v >> 5) & 0x3F) * 255 / 63) * w;
          b += (int)((v & 0x1F) * 255 / 31) * w;
          a += (int)p[2] * w;
        }
      }
      r >>= 8; g >>= 8; b >>= 8; a >>= 8;
      const uint16_t out = (uint16_t)((r * 31 / 255) << 11) |
                           (uint16_t)((g * 63 / 255) << 5) |
                           (uint16_t)(b * 31 / 255);
      uint8_t* q = dst + (y * dw + x) * 3;
      q[0] = (uint8_t)(out & 0xFF);
      q[1] = (uint8_t)(out >> 8);
      q[2] = (uint8_t)(a > 255 ? 255 : a);
    }
  }
}
