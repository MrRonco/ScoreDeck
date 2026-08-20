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

// ── STENCILS ───────────────────────────────────────────────────────────────
//
// The disc is bigger than the job. Three regions of it were measured doing
// active harm on --scenario 10 / --scenario 3, and all three are removed by
// multiplying the alpha, not by shrinking the sprite (the sprite's size is
// what puts its centre on the digits).
//
// FOOT. The card's bottom 52 px is the footer band — the situation line and
// the broadcast label sit at card y 218, their glyphs at 221..231 with about
// 2 px of AA either side. With the glow reaching them, s_footR measured
// 2.67:1 against its AA-masked local background against a 4.5:1 floor. The
// hero's score digits end at card y 202, so the ramp runs 200 -> 216: three
// clean rows above the footer's first AA pixel, and the digits' last row
// dimmed by 0.37 L*, which is inside the dither's own +/-2.2 L* per row.
//
// 16 rows, not 8, and not a hard chord. A chord would draw a straight
// horizontal line across a circle, which is the one shape a glow must never
// have — but so does a ramp that is too short. At 8 rows the drop measured
// up to 6.6 L* per row against the glow's own 0.18 L* per row falloff at that
// radius, and it read as an edge at 2x. Over 16 it averages 1.2 L* per row,
// under the dither noise, and dissolves.
#define BLOOM_FOOT_CLEAR  52   // rows at the card's foot the glow may not enter
#define BLOOM_FOOT_TAPER  16   // rows the alpha ramps to zero over

// EDGE. The card is 508 wide with a 1 px light border; the sprite starts at
// card x 357, so only 151 of its 220 columns are on the card at all and the
// last of those IS the border. Drawn flat to that boundary the glow both
// erased the border (154 of the 232 rows where x=16 and x=523 are true border
// pixels differed) and left the last interior column at L* 35.59 against the
// hero fill's 18.00. Taper the last 24 visible columns instead of the whole
// disc: a smoothstep reaching zero at the disc's own edge would dim the right
// half of the digits this thing exists to light, which is the opposite of the
// point. Measured: the digits reach card x 482 and 24 columns of taper cost
// them 0.08 L* there, against the 16.49 L* it takes off the border column.
#define BLOOM_EDGE_TAPER  24   // columns the alpha ramps to zero over

/** 0 at t<=0, 1 at t>=1, C1 in between. Hermite, not linear: a linear ramp
 *  leaves a visible crease at both ends of the taper on a 5-bit channel. */
static inline float bloomStep(float t) {
  if (t <= 0.0f) return 0.0f;
  if (t >= 1.0f) return 1.0f;
  return t * t * (3.0f - 2.0f * t);
}

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

// The soft sprite is built ON DEMAND, and on the panel that demand never
// arrives. Measured: the only firmware call into bloomSet() is ui_hero.cpp's
// hide path, which passes opa 0 and returns before any source is read — so
// this 220*220*3 = 145,200 byte buffer was allocated at boot, filled, and
// never drawn a single time. Worse, bloomCreate() used to return nullptr when
// the allocation failed, which gated the ENTIRE effect — including the
// pre-composited path, which does not use this buffer at all — on 141.8 KB of
// PSRAM that no panel pixel depends on.
//
// It is not dead code, though, and deleting it would have been wrong: three
// call sites in desktop/spike.cpp draw it at LV_OPA_COVER to cost the soft
// path against the composited one. So it stays, and it stays lazy. bloomInit()
// is now the declaration of intent and nothing else.
static bool bloomSoftSprite() {
  if (s_buf) return true;
  s_buf = (uint8_t*)heap_caps_malloc(BLOOM_S * BLOOM_S * 3, MALLOC_CAP_SPIRAM);
  if (!s_buf) return false;

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
  return true;
}

void bloomInit() {
  // Deliberately empty — see bloomSoftSprite(). Kept so the boot sequence in
  // main.cpp still names the subsystem it is bringing up.
}

lv_obj_t* bloomCreate(lv_obj_t* parent, int w, int h) {
  lv_obj_t* o = lv_img_create(parent);

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
  // The soft sprite is bound HERE, not at create time, because this is the
  // only path that ever reads it — the composited path binds its own slot in
  // bloomComposite(). Nothing on the panel reaches this line.
  if (!bloomSoftSprite()) return;
  lv_img_set_src(o, &s_dsc);
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
 *  4. THE STENCIL IS ALSO GEOMETRIC. Light is not an excuse to overwrite the
 *     things around it. Two card-relative ramps (BLOOM_FOOT_CLEAR /
 *     BLOOM_EDGE_TAPER above) multiply the alpha so the glow cannot reach the
 *     footer band or the card's own border, and everything that falls off the
 *     card is skipped rather than composited. Both are ramps and neither is
 *     large enough to touch the digits.
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

  // Where this sprite sits inside the card. Two things need it: the opaque
  // region must be kept OFF the specular pair — glassPanel draws a 1 px
  // highlight at the top of the content area and a 2 px shade at the bottom,
  // as children 0 and 1, they must keep drawing over the bloom, and an opaque
  // pixel there would punch a hole in the highlight (the disc overlaps ~100 px
  // of it) — and the foot/edge stencils are both expressed in card rows and
  // columns.
  //
  // update_layout FIRST, and this is not defensive. LVGL 8 does not apply
  // lv_obj_set_pos/set_size immediately — it marks the screen's layout dirty
  // and resolves it in the refresh pass. bloomComposite runs from the poll,
  // BEFORE that pass, so every coordinate here read back as zero: measured,
  // the card reported 0x0 at every single composite, which means the specular
  // row test below had never once evaluated true since the day it was written
  // and the docblock's claim to protect the highlight was not being kept. It
  // now is. (No pixel moves from that: the only sprite reaching the top strips
  // is the away-lead one, and its alpha there is under 5 of 255 — the top
  // border row measures bit-identical before and after.)
  //
  // Offsets from coords, NOT lv_obj_get_y(): that subtracts the parent's
  // border width, so on a 1 px-bordered glassPanel it reports one row above
  // where the sprite actually starts. The specular test is restated in that
  // true frame, so it still names rows 0..3 and cardH-3..cardH-1.
  lv_obj_update_layout(o);
  lv_obj_t* par   = lv_obj_get_parent(o);
  lv_area_t oa, pa;
  lv_obj_get_coords(o, &oa);
  if (par) lv_obj_get_coords(par, &pa);
  const int xOff  = par ? (int)(oa.x1 - pa.x1) : 0;
  const int yOff  = par ? (int)(oa.y1 - pa.y1) : lv_obj_get_y(o);
  const int cardW = par ? lv_obj_get_width(par)  : 0;
  const int cardH = par ? lv_obj_get_height(par) : 0;
  const int footCut = cardH - BLOOM_FOOT_CLEAR;
  const int edgeX   = cardW - 1;                 // the card's right border column

  for (int y = 0; y < BLOOM_S; y++) {
    const int cardY = yOff + y;
    // ROW STENCIL — the foot of the card, plus anything off the card entirely.
    float wy = 1.0f;
    if (cardH) {
      if (cardY < 0 || cardY >= cardH) wy = 0.0f;
      else wy = bloomStep((float)(footCut - cardY) / (float)BLOOM_FOOT_TAPER);
    }
    const bool onSpecular = cardH && (cardY < 4 || cardY > cardH - 4);
    for (int x = 0; x < BLOOM_S; x++) {
      uint8_t* p = sl->buf + (y * BLOOM_S + x) * 3;

      // COLUMN STENCIL — the right border, plus the 69 columns of this sprite
      // that hang off the card and were being composited into a buffer nobody
      // reads. d is the distance to the border column; d <= 1 is the border
      // itself and the last interior column, both of which must read as card.
      float w = wy;
      if (w > 0.0f && cardW) {
        const int d = edgeX - (xOff + x);
        w *= bloomStep((float)(d - 1) / (float)BLOOM_EDGE_TAPER);
      }
      if (w <= 0.0f) { p[0] = 0; p[1] = 0; p[2] = 0; continue; }

      const float dx = (x - c) / c, dy = (y - c) / c;
      float r = sqrtf(dx * dx + dy * dy);

      if (r >= 1.0f) { p[0] = 0; p[1] = 0; p[2] = 0; continue; }
      const float f = (1.0f - r) * (1.0f - r);
      // 200, not 255: the soft path drew this sprite at img_opa 200, and the
      // pre-composited path has to reproduce the SAME intensity or the glow
      // arrives 27% hotter and its gradient correspondingly steeper.
      const float a = f * 200.0f * w;

      // OPAQUE CORE: we did the blend ourselves, so LVGL copies these pixels
      // and none of its 26-level opacity quantisation touches them. This is
      // the part that removes the rings.
      //
      // SOFT RIM: past 0.92 of the radius — and anywhere the sprite crosses
      // the specular strips — fall back to letting LVGL blend the team colour
      // at the true alpha. Both paths compute the SAME ideal value; the rim's
      // contribution is under two 565 steps, so quantising it costs nothing
      // visible, and it keeps the seam from ever becoming a hard circle.
      //
      // INSIDE A STENCIL RAMP the opaque path runs all the way down to zero
      // instead. A ramp IS a gradient, and handing a gradient to LVGL's blend
      // is the 26-level staircase this whole file exists to avoid. Measured
      // both ways on --scenario 10: 359 pixels differ by up to 2.36 L*, and
      // the 4x4-cell error against the continuous ideal goes from a mean of
      // 0.153 L* with 9 cells over 0.7 to a mean of 0.136 with 5. Small, but
      // it is free and it is the argument this file already makes.
      //
      // The dither amplitude scales with the contribution below `thresh`, so
      // the ramp arrives at EXACTLY the card fill at its foot rather than at a
      // dithered approximation of it — note 2 above, applied to alpha.
      const bool ramped = (w < 1.0f);
      if (r < 0.92f && !onSpecular && (a >= (float)thresh || ramped)) {
        const float t = (kBayer4[(y & 3) * 4 + (x & 3)] + 0.5f) / 16.0f - 0.5f;
        const float ds = a >= (float)thresh ? 1.0f : a / (float)thresh;
        // Dither the INCREMENT, each channel by half its own 565 step. The
        // absolute value must not be dithered: the card fill is not exactly
        // representable in 565, so that would texture the flat background too.
        int rr = fr + (int)(dr * a / 255.0f + 8.0f * t * ds + 0.5f);
        int gg = fg + (int)(dg * a / 255.0f + 4.0f * t * ds + 0.5f);
        int bb = fb + (int)(db * a / 255.0f + 8.0f * t * ds + 0.5f);
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
