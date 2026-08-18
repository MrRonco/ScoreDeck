// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
// bloom.cpp — the signature: light in the leading team's colour.
//
// UI.md §2 has described this from the start — "the bloom is a small pre-baked
// alpha sprite recoloured via img_recolor" — and nothing ever built it. The
// edge light that was meant to carry the signature has been demoted twice
// since, from a full-perimeter strip to a row marker to a 4x52 px stub. It is
// now the smallest object on the screen, which is why it reads as trim rather
// than as an identity.
//
// WHY LIGHT. Everything else on this panel has hard edges: type, hairlines,
// 1 px specular catches, rounded rects. Soft against hard is a contrast axis
// the design currently spends NOTHING on, so introducing exactly one soft
// element makes it the signature by construction. It is also the one thing a
// printed scoreboard physically cannot do — it says "emissive" in a way no
// amount of layout can.
//
// ONE sprite, recoloured per team. Alpha carries the falloff and the RGB is
// white, so lv_img_set_recolor() at full opacity tints the whole thing to the
// team's lifted colour. That is 27 KB of PSRAM total for every team in every
// league, rather than an asset each.
#include "ui.h"
#include "theme.h"
#include "../config.h"
#include <esp_heap_caps.h>
#include <math.h>

// Generated at FINAL size. An earlier version generated 96 px and zoomed to
// 220 with LV_IMG_SIZE_MODE_REAL and a 0,0 pivot; the sprite then landed well
// right of the position it was given — measurably, the lift appeared only in
// the rightmost 30 px of where it should have been. Rather than reverse-
// engineer lv_img's scaling anchor, generate the pixels we actually want.
// 220x220x3 = 145 KB of PSRAM, once, against 8 MB.
#define BLOOM_S 220

static uint8_t*     s_buf;
static lv_img_dsc_t s_dsc;

void bloomInit() {
  if (s_buf) return;
  s_buf = (uint8_t*)heap_caps_malloc(BLOOM_S * BLOOM_S * 3, MALLOC_CAP_SPIRAM);
  if (!s_buf) return;

  const float c = (BLOOM_S - 1) / 2.0f;
  for (int y = 0; y < BLOOM_S; y++) {
    for (int x = 0; x < BLOOM_S; x++) {
      const float dx = (x - c) / c, dy = (y - c) / c;
      float r = sqrtf(dx * dx + dy * dy);
      if (r > 1.0f) r = 1.0f;
      // QUADRATIC, not quartic — and this is an RGB565 constraint, not taste.
      //
      // The first version squared this again for a softer edge. It rendered,
      // and it was invisible: RGB565's 5-bit blue needs a delta of 8 before
      // ANY change survives quantisation, and a quartic falloff is under that
      // for most of the radius.
      //
      // NOTE the arithmetic below is illustrative only. It was written as
      // (255-66)*alpha/255 from THIS sprite's white blue channel, but
      // lv_img_set_recolor replaces the sprite's colour with the team's
      // before the blend runs, so the real delta depends on the team (for a
      // deep-blue kit it is ~1 of 31, not 189). The CONCLUSION — quadratic,
      // not quartic — is unaffected and was confirmed on the panel. Quartic put the
      // delta at 0.6 by three quarters of the radius, so everything outside
      // the middle third rounded away to exactly the card colour — which is
      // why the card fill measured identical 60 px either side of the digit.
      //
      //   quartic    r=0.50 -> 9.3    r=0.75 -> 0.6   (lost)
      //   quadratic  r=0.50 -> 37.1   r=0.75 -> 9.3   (survives)
      //
      // A gentler curve is not "less soft" here. On a 5-bit channel it is the
      // difference between a gradient and nothing at all.
      const float f = (1.0f - r) * (1.0f - r);

      // ORDERED DITHER on the alpha, for the same reason plate.cpp dithers:
      // this is a gradient on a 5/6/5 panel. Alpha is the ONLY variable here
      // and it feeds lv_color_mix, which quantises opacity to (opa + 4) >> 3
      // — 26 blend levels, not 256 — so a wide span of radii collapses onto
      // one output value and the falloff reads as concentric rings. Measured
      // before this change: 16 flat colour bands across the glow, runs up to
      // 67 px wide.
      //
      // The amplitude is exactly one blend step (8 units of opa_tmp = 10.24
      // alpha units at the img_opa this sprite is drawn with), so each hard
      // step edge becomes a 4x4 interleave of the two values that already
      // bracket it. It cannot invent detail the channel cannot carry — the
      // rim survives, and Phase 6 is what removes it — but it halves the
      // visible run length for four lines and no runtime cost.
      static const uint8_t kBayer4[16] = { 0, 8, 2, 10, 12, 4, 14, 6,
                                           3, 11, 1,  9, 15, 7, 13, 5 };
      uint8_t a;
      if (f <= 0.0f) {
        a = 0;                      // outside the disc stays exactly clear:
                                    // dithering here would ring the boundary
      } else {
        const float t = (kBayer4[(y & 3) * 4 + (x & 3)] + 0.5f) / 16.0f - 0.5f;
        float av = f * 255.0f + 10.24f * t;
        if (av < 0.0f)   av = 0.0f;
        if (av > 255.0f) av = 255.0f;
        a = (uint8_t)(av + 0.5f);
      }

      uint8_t* p = s_buf + (y * BLOOM_S + x) * 3;
      p[0] = 0xFF; p[1] = 0xFF;      // white RGB565; recolour supplies the hue
      p[2] = a;
    }
  }
  s_dsc.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
  s_dsc.header.always_zero = 0;
  s_dsc.header.w = BLOOM_S;
  s_dsc.header.h = BLOOM_S;
  s_dsc.data_size = BLOOM_S * BLOOM_S * 3;
  s_dsc.data = s_buf;
}

lv_obj_t* bloomCreate(lv_obj_t* parent, int w, int h) {
  if (!s_buf) return nullptr;
  lv_obj_t* o = lv_img_create(parent);
  lv_img_set_src(o, &s_dsc);
  // No zoom, no pivot, no size mode — the sprite is already the right size.
  (void)w; (void)h;
  lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  return o;
}

void bloomSet(lv_obj_t* o, uint32_t colour, lv_opa_t opa) {
  if (!o) return;
  if (!opa) { lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN); return; }
  // LV_IMG_CACHE_DEF_SIZE is 0, so the recolour blend is recomputed on every
  // DRAW of this object — it is not cached. That is affordable here because
  // the hero repaints only when its data changes, but it means this must never
  // be called on an object that repaints continuously, and the caller must
  // change-cache the colour so an unchanged write does not invalidate.
  lv_obj_set_style_img_recolor(o, lv_color_hex(colour), 0);
  lv_obj_set_style_img_recolor_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_img_opa(o, opa, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
}
