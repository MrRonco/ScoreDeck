// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
#pragma once
#include <stdint.h>

/** Bilinear-scale an RGB565+A8 buffer (3 bytes/px, 565 little-endian). See
 *  imgscale.cpp for why lv_img's zoom is never used on logos. */
void imgScaleRgb565A8(const uint8_t* src, int sw, int sh,
                      uint8_t* dst, int dw, int dh);
