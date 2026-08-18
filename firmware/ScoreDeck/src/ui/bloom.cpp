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
#include <string.h>

// Generated at FINAL size. An earlier version generated 96 px and zoomed to
// 220 with LV_IMG_SIZE_MODE_REAL and a 0,0 pivot; the sprite then landed well
// right of the position it was given — measurably, the lift appeared only in
// the rightmost 30 px of where it should have been. Rather than reverse-
// engineer lv_img's scaling anchor, generate the pixels we actually want.
// 220x220x3 = 145 KB of PSRAM, once, against 8 MB.
#define BLOOM_S 220

static uint8_t*     s_buf;          // the shared WHITE alpha sprite (soft path)
static lv_img_dsc_t s_dsc;

// PRE-COMPOSITED sprites, one per bloom object.
//
// The soft path above hands LVGL an alpha ramp and lets it blend at draw time,
// and that blend is the whole problem: lv_color_mix quantises opacity to
// (opa + 4) >> 3 — 26 levels — so a smooth radius collapses onto 26 outputs
// and the falloff reads as rings. Dithering the alpha (see below) spreads the
// boundaries but cannot add levels that the blend does not have.
//
// So this path does the blend OURSELVES, in 8-bit, against the known card
// fill, dithers the INCREMENT, quantises once to RGB565 and hands LVGL an
// OPAQUE image it can copy without blending. No runtime blend, no 26-level
// quantisation, no rings.
#define BLOOM_SLOTS 2
static struct BloomSlot {
  lv_obj_t*    obj;
  uint8_t*     buf;
  lv_img_dsc_t dsc;
} s_slot[BLOOM_SLOTS];
static uint8_t s_slotN;

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

  // Each bloom gets its OWN composited buffer: the two sides of a game are
  // different teams, so a shared sprite cannot carry both pre-blended.
  // Slots are REUSED, not consumed. uiHeroInit runs again on every rebuild
  // (density change, rail toggle, settings close), so a create-once registry
  // would hand the second build no buffer and the glow would silently never
  // draw again — while leaking 145 KB of PSRAM per rebuild. Exactly
  // BLOOM_SLOTS blooms are alive at a time, so slot k always belongs to the
  // k-th bloom of the CURRENT build.
  {
    BloomSlot& sl = s_slot[s_slotN % BLOOM_SLOTS];
    sl.obj = o;
    if (!sl.buf) {
      sl.buf = (uint8_t*)heap_caps_malloc(BLOOM_S * BLOOM_S * 3, MALLOC_CAP_SPIRAM);
      if (sl.buf) {
        memset(sl.buf, 0, BLOOM_S * BLOOM_S * 3);
        sl.dsc.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
        sl.dsc.header.always_zero = 0;
        sl.dsc.header.w = BLOOM_S;
        sl.dsc.header.h = BLOOM_S;
        sl.dsc.data_size = BLOOM_S * BLOOM_S * 3;
        sl.dsc.data = sl.buf;
      }
    }
    if (sl.buf) s_slotN++;
  }
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


/**
 * Pre-composite this bloom over `fill` in the team's colour.
 *
 * Three things make this different from simply tinting the alpha sprite:
 *
 *  1. THE BLEND IS DONE HERE, in 8 bits, so the output is an opaque image and
 *     LVGL never runs lv_color_mix over it. That is what removes the rings.
 *  2. THE INCREMENT IS DITHERED, not the absolute value. Dithering the
 *     absolute leaves a visible 220x220 textured patch, because the card fill
 *     itself is not exactly representable in RGB565 and the noise therefore
 *     lands on the flat background too. Dithering only the glow's CONTRIBUTION
 *     means the noise scales to zero exactly where the glow does.
 *  3. THE STENCIL IS ADAPTIVE. Alpha is 0 wherever the glow's contribution
 *     would quantise away to the fill anyway — computed from THIS team's own
 *     channel deltas — so the hard edge is provably invisible rather than
 *     merely faint. The opaque disc is additionally capped well inside the
 *     sprite so it can never reach the card's 1 px specular highlight or its
 *     2 px bottom shade, which are drawn by glassPanel as children 0 and 1 and
 *     must keep drawing OVER this (glassRelayout indexes them positionally, so
 *     the bloom cannot be moved below them).
 */
void bloomComposite(lv_obj_t* o, uint32_t colour, uint32_t fill) {
  BloomSlot* sl = nullptr;
  for (uint8_t i = 0; i < s_slotN; i++) if (s_slot[i].obj == o) { sl = &s_slot[i]; break; }
  if (!sl || !sl->buf) return;

  const int fr = (fill >> 16) & 0xFF, fg = (fill >> 8) & 0xFF, fb = fill & 0xFF;
  const int tr = (colour >> 16) & 0xFF, tg = (colour >> 8) & 0xFF, tb = colour & 0xFF;
  const int dr = tr - fr, dg = tg - fg, db = tb - fb;

  // One RGB565 step is 8 units of red/blue and 4 of green. The glow is
  // invisible once every channel's contribution is under half a step.
  int maxd = abs(dr) > abs(db) ? abs(dr) : abs(db);
  if (abs(dg) * 2 > maxd) maxd = abs(dg) * 2;      // green's step is half
  int thresh = maxd > 0 ? (4 * 255) / maxd : 255;  // alpha below this is a no-op
  if (thresh < 6)   thresh = 6;
  if (thresh > 40)  thresh = 40;

  static const uint8_t kBayer4[16] = { 0, 8, 2, 10, 12, 4, 14, 6,
                                       3, 11, 1,  9, 15, 7, 13, 5 };
  const float c = (BLOOM_S - 1) / 2.0f;

  // Where this sprite sits inside the card, so the opaque region can be kept
  // OFF the specular pair. glassPanel draws a 1 px highlight at the top of the
  // content area and a 2 px shade at the bottom, as children 0 and 1; they
  // must keep drawing over the bloom, and an opaque pixel there would punch a
  // hole in the highlight (the disc overlaps ~100 px of it).
  const int yOff  = lv_obj_get_y(o);
  lv_obj_t* par   = lv_obj_get_parent(o);
  const int cardH = par ? lv_obj_get_height(par) : 0;

  for (int y = 0; y < BLOOM_S; y++) {
    const int cardY = yOff + y;
    const bool onSpecular = cardH && (cardY < 3 || cardY > cardH - 5);
    for (int x = 0; x < BLOOM_S; x++) {
      const float dx = (x - c) / c, dy = (y - c) / c;
      float r = sqrtf(dx * dx + dy * dy);
      uint8_t* p = sl->buf + (y * BLOOM_S + x) * 3;

      if (r >= 1.0f) { p[0] = 0; p[1] = 0; p[2] = 0; continue; }
      const float f = (1.0f - r) * (1.0f - r);
      // 200, not 255: the soft path drew this sprite at img_opa 200, and the
      // pre-composited path has to reproduce the SAME intensity or the glow
      // arrives 27% hotter and its gradient correspondingly steeper.
      const float a = f * 200.0f;

      // OPAQUE CORE: we did the blend ourselves, so LVGL copies these pixels
      // and none of its 26-level opacity quantisation touches them. This is
      // the part that removes the rings.
      //
      // SOFT RIM: past 0.92 of the radius — and anywhere the sprite crosses
      // the specular strips — fall back to letting LVGL blend the team colour
      // at the true alpha. Both paths compute the SAME ideal value; the rim's
      // contribution is under two 565 steps, so quantising it costs nothing
      // visible, and it keeps the seam from ever becoming a hard circle.
      if (r < 0.92f && !onSpecular && a >= (float)thresh) {
        const float t = (kBayer4[(y & 3) * 4 + (x & 3)] + 0.5f) / 16.0f - 0.5f;
        // Dither the INCREMENT, each channel by half its own 565 step. The
        // absolute value must not be dithered: the card fill is not exactly
        // representable in 565, so that would texture the flat background too.
        int rr = fr + (int)(dr * a / 255.0f + 8.0f * t + 0.5f);
        int gg = fg + (int)(dg * a / 255.0f + 4.0f * t + 0.5f);
        int bb = fb + (int)(db * a / 255.0f + 8.0f * t + 0.5f);
        if (rr < 0) rr = 0; if (rr > 255) rr = 255;
        if (gg < 0) gg = 0; if (gg > 255) gg = 255;
        if (bb < 0) bb = 0; if (bb > 255) bb = 255;
        const uint16_t v = (uint16_t)(((rr >> 3) << 11) | ((gg >> 2) << 5) | (bb >> 3));
        p[0] = (uint8_t)(v & 0xFF);
        p[1] = (uint8_t)(v >> 8);
        p[2] = 0xFF;
      } else {
        const uint16_t v = (uint16_t)(((tr >> 3) << 11) | ((tg >> 2) << 5) | (tb >> 3));
        p[0] = (uint8_t)(v & 0xFF);
        p[1] = (uint8_t)(v >> 8);
        p[2] = (uint8_t)(a + 0.5f);     // let LVGL blend the faint remainder
      }
    }
  }

  lv_img_set_src(o, &sl->dsc);
  // The recolour must be OFF: the pixels already carry the team's colour.
  lv_obj_set_style_img_recolor_opa(o, LV_OPA_TRANSP, 0);
  lv_obj_set_style_img_opa(o, LV_OPA_COVER, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
  lv_obj_invalidate(o);
}
