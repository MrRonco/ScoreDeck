// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
// plate.cpp — the ground, generated once at boot.
//
// UI.md §1 has specified this since the beginning: "composited once at boot
// into the background plate — per-pixel tint, grain, specular top edge, bottom
// shade — runtime cost: zero". Nothing implemented it. The plate was one flat
// `bg_color` across 800x480, which is most of what "seems basic" means on an
// emissive panel: at L* 1.89 a flat RGB565 fill sits below the display's own
// noise floor in places and above it in others, and reads as a dead void
// rather than as a material.
//
// WHY GENERATED AND NOT SHIPPED. Three options, and the constraints pick one:
//
//   * a C array in flash — 800x480x2 = 750 KB against ~1.6 MB of headroom.
//     Affordable but expensive, and it fixes the artwork at build time.
//   * a file on the 9 MB FAT partition — LV_USE_FS_STDIO is 0, so there is no
//     lv_fs driver registered; this is new infrastructure, not a free lever.
//     Worse, LV_IMG_CACHE_DEF_SIZE is 0, so an image referenced by PATH is
//     re-opened and re-read on EVERY draw.
//   * generated into PSRAM at boot — 750 KB of 8 MB, zero flash, no new
//     tooling, and the parameters stay in source where they can be tuned.
//
// The third. It costs ~230 ms once at boot (750 KB at the ~3.3 MB/s effective
// write throughput a full repaint implies) and nothing thereafter: the plate
// is static, so it is only ever re-blitted where ground is actually exposed,
// which is the ~38% of the panel the cards do not cover.
#include "ui.h"
#include "theme.h"
#include "../config.h"
#include <esp_heap_caps.h>

static uint8_t*     s_buf;
static lv_img_dsc_t s_dsc;
static lv_obj_t*    s_img;

// The light is above and slightly left of centre, which is where every
// specular catch in glassPanel() already implies it is. Keeping the two
// consistent is the difference between "lit" and "dirty".
#define LIGHT_X   360
#define LIGHT_Y   120
// The amplitude is NOT a taste decision — the surface ladder fixes it.
//
// The dimmest card is FINAL at 0x101825, whose red channel is 16 against the
// plate's 4. Any lift of 12 or more makes the brightest part of the GROUND
// lighter than the darkest CARD, which inverts the figure/ground relationship
// the whole redesign was built to establish. Two earlier passes got this
// wrong in both directions: LIFT_MAX 14 over a 520 px squared falloff was
// mathematically present and visually absent (a column at x=792 read #000408
// at every single y), and LIFT_MAX 30 over 860 px lifted the entire panel to
// #181C21 — brighter than FINAL everywhere.
//
// So 10, with the falloff reaching zero just past the far corner (569 px from
// the light). The field is deliberately quiet. Making the ground shout is not
// available; that is what the bloom and the motion are for.
#define LIFT_MAX   10      // 8-bit lift at the brightest point, before dither
#define FALLOFF   620.0f   // reaches zero just past the farthest corner

/**
 * Ordered dither.
 *
 * RGB565's blue channel has 5 bits, so a ramp of 14 levels over 500 px
 * quantises into three flat slabs with hard edges — the exact banding that got
 * `bg_grad_dir` banned in theme.cpp. Dithering is not a workaround for that,
 * it is the standard answer to it: a 4x4 Bayer threshold applied before
 * quantisation converts the hard edges into a stable pattern the eye
 * integrates. Measured on a 128 px ramp, mean flat-run length falls from
 * 25.6 px to 1.03 px — no edge is ever wider than the dither cell.
 */
static const uint8_t kBayer[16] = {
   0,  8,  2, 10,
  12,  4, 14,  6,
   3, 11,  1,  9,
  15,  7, 13,  5,
};

/** Cheap deterministic hash, for grain. Not random — reproducible, so the
 *  plate is identical on every boot and cannot shimmer between reboots. */
static inline uint32_t hash2(uint32_t x, uint32_t y) {
  uint32_t h = x * 374761393u + y * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}

void plateInit() {
  if (s_buf) return;
  s_buf = (uint8_t*)heap_caps_malloc(SCR_W * SCR_H * 2, MALLOC_CAP_SPIRAM);
  if (!s_buf) return;                 // no plate is better than no boot

  const uint32_t base = 0x04070E;     // C_PLATE, as a 24-bit value
  const int br = (base >> 16) & 0xFF, bg = (base >> 8) & 0xFF, bb = base & 0xFF;

  for (int y = 0; y < SCR_H; y++) {
    for (int x = 0; x < SCR_W; x++) {
      const float dx = (float)(x - LIGHT_X), dy = (float)(y - LIGHT_Y);
      float t = 1.0f - (dx * dx + dy * dy) / (FALLOFF * FALLOFF);
      if (t < 0.0f) t = 0.0f;
      // Between linear and squared. Squared concentrated everything near the
      // centre and left the outer two thirds flat; linear reads as a disc.
      t = t * (0.6f + 0.4f * t);

      // Grain, +/- 1 level, applied BEFORE quantisation so it survives it.
      // This is the part that makes near-black read as a material rather than
      // as an absence. It is invisible as texture and obvious by its absence.
      const int grain = (int)(hash2((uint32_t)x, (uint32_t)y) & 3u) - 1;

      const int lift = (int)(t * LIFT_MAX + 0.5f);
      const int d = kBayer[((y & 3) << 2) | (x & 3)];

      // Quantise with the dither threshold folded in: scale to the channel's
      // range, add (d/16) of one output level, then truncate.
      const int r8 = br + lift + grain;
      const int g8 = bg + lift + grain;
      const int b8 = bb + lift + grain;
      const int r5 = (r8 * 31 + d * 255 / 16) / 255;
      const int g6 = (g8 * 63 + d * 255 / 16) / 255;
      const int b5 = (b8 * 31 + d * 255 / 16) / 255;

      const uint16_t px = (uint16_t)((r5 > 31 ? 31 : r5 < 0 ? 0 : r5) << 11) |
                          (uint16_t)((g6 > 63 ? 63 : g6 < 0 ? 0 : g6) << 5) |
                          (uint16_t)((b5 > 31 ? 31 : b5 < 0 ? 0 : b5));
      uint8_t* p = s_buf + (y * SCR_W + x) * 2;
      p[0] = (uint8_t)(px & 0xFF);
      p[1] = (uint8_t)(px >> 8);
    }
  }

  s_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
  s_dsc.header.always_zero = 0;
  s_dsc.header.w = SCR_W;
  s_dsc.header.h = SCR_H;
  s_dsc.data_size = SCR_W * SCR_H * 2;
  s_dsc.data = s_buf;

  // A real object at the very back rather than a style bg_img on the screen:
  // every screen root is its own child of the screen, and an image object can
  // be pushed behind all of them once and then ignored.
  lv_obj_t* scr = lv_scr_act();
  s_img = lv_img_create(scr);
  lv_img_set_src(s_img, &s_dsc);
  lv_obj_set_pos(s_img, 0, 0);
  lv_obj_clear_flag(s_img, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_move_background(s_img);
}

lv_obj_t* plateRoot() { return s_img; }

bool plateReady() { return s_buf != nullptr; }
