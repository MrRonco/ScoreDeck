// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
#include "theme.h"
#include <math.h>
#include "../config.h"

// Generated faces — UI.md §9. Archivo Condensed for display, IBM Plex for text.
//
// Two things are load-bearing here and neither is taste:
//   * tnum is FROZEN on every face that renders changing numbers, or the digits
//     visibly jitter as scores tick over.
//   * body carries Latin-1 Supplement AND Latin Extended-A (~350 glyphs).
//     Doncic, Odegaard, Konate and Vlasic are boxes in a 7-bit ASCII face, and
//     the lineup screens are made of exactly those names.
//
// Both families are OFL-1.1 — see THIRD-PARTY-NOTICES.md.
LV_FONT_DECLARE(font_score46)
LV_FONT_DECLARE(font_score38)
LV_FONT_DECLARE(font_display30)
LV_FONT_DECLARE(font_abbr17)
LV_FONT_DECLARE(font_body15)
LV_FONT_DECLARE(font_micro11)
LV_FONT_DECLARE(font_micro13)
LV_FONT_DECLARE(font_num15)
// The two focal faces. Both are 2 bpp rather than 4 — at 72 px and 96 px the
// edge is many pixels long, so the extra two bits of coverage buy nothing a
// desk viewer can see, and they halve the cost of the largest tables we ship.
LV_FONT_DECLARE(font_hero72)    // digits + '-' ':' 'H' 'M'  — 36 KB
LV_FONT_DECLARE(font_clock96)   // digits + ':'              — 47 KB

const lv_font_t* F_SCORE = &font_score38;
const lv_font_t* F_SCORE_BIG = &font_score46;
const lv_font_t* F_HERO  = &font_hero72;
const lv_font_t* F_CLOCK = &font_clock96;
const lv_font_t* F_DISPLAY = &font_display30;
const lv_font_t* F_ABBR  = &font_abbr17;
const lv_font_t* F_BODY  = &font_body15;
const lv_font_t* F_NUM   = &font_num15;
const lv_font_t* F_MICRO = &font_micro13;

static lv_style_t s_glass;
static lv_style_t s_glassPressed;
static lv_style_t s_badge;

// ── team colour normalisation ──────────────────────────────────────────────
//
// ~3,000 team colours arrive as content and roughly a fifth of them are navy,
// black or deep maroon. Toronto's #00205B against the tile is 1.11:1 — the
// signature element of the product, invisible on its own flagship example.
//
// The threshold is a CONTRAST RATIO, not a brightness cutoff. That distinction
// is load-bearing: a brightness rule tuned on Toronto also fires on Kansas
// City (#E71831) and Edmonton (#FF4C00), which are already at 3.75 and 5.15,
// and washes out colours that were never the problem.

/** sRGB -> linear, 8-bit domain, 16-bit range. Built once, ~10 cycles a lookup. */
static uint16_t s_lin[256];

static void buildLinearLut() {
  for (int i = 0; i < 256; i++) {
    const float c = i / 255.0f;
    const float l = (c <= 0.03928f) ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
    s_lin[i] = (uint16_t)(l * 65535.0f + 0.5f);
  }
}

static inline float relLum(uint32_t c) {
  return (0.2126f * s_lin[(c >> 16) & 0xFF] +
          0.7152f * s_lin[(c >> 8) & 0xFF] +
          0.0722f * s_lin[c & 0xFF]) / 65535.0f;
}

static float contrast(uint32_t a, uint32_t b) {
  const float la = relLum(a) + 0.05f, lb = relLum(b) + 0.05f;
  return (la > lb) ? la / lb : lb / la;
}

/** Two stages, in this order — the order is the whole point. Saturating first
 *  keeps the hue exact, so Toronto comes out MORE blue than it went in. A
 *  single blend toward white reaches the same ratio but greys the colour, and
 *  at this viewing distance a 3px strip resolves hue, not just brightness. */
static uint32_t lift(uint32_t c, uint32_t against, float minRatio) {
  if (contrast(c, against) >= minRatio) return c;      // already fine, leave it

  const int r0 = (c >> 16) & 0xFF, g0 = (c >> 8) & 0xFF, b0 = c & 0xFF;
  const int mx = r0 > g0 ? (r0 > b0 ? r0 : b0) : (g0 > b0 ? g0 : b0);

  // Stage 1: raise value toward full saturation, hue exact. Walk it rather
  // than jumping — going straight to max overshoots badly for a low target
  // and turns a navy badge into electric blue when a nudge would have done.
  int r = r0, g = g0, b = b0;
  if (mx > 0) {
    for (int t = 1; t <= 20; t++) {
      const int k = 255 * t / 20;                      // interpolate 0 -> full
      const int rr = r0 + (r0 * 255 / mx - r0) * k / 255;
      const int gg = g0 + (g0 * 255 / mx - g0) * k / 255;
      const int bb = b0 + (b0 * 255 / mx - b0) * k / 255;
      const uint32_t out = (uint32_t)rr << 16 | (uint32_t)gg << 8 | (uint32_t)bb;
      if (contrast(out, against) >= minRatio) return out;
      r = rr; g = gg; b = bb;
    }
  }
  // Stage 2: only now start losing chroma.
  for (int t = 1; t <= 100; t++) {
    const uint32_t out = (uint32_t)(r + (255 - r) * t / 100) << 16 |
                         (uint32_t)(g + (255 - g) * t / 100) << 8 |
                         (uint32_t)(b + (255 - b) * t / 100);
    if (contrast(out, against) >= minRatio) return out;
  }
  return 0xFFFFFF;
}

uint32_t teamInkOn(uint32_t color, uint32_t surface, float minRatio) {
  return lift(color, surface, minRatio);
}

uint32_t teamInk(uint32_t color) {
  return lift(color, 0x1B2636, 3.5f);      // against the live tile fill
}

uint32_t teamFill(uint32_t color) {
  // A badge only has to separate from the plate; its own label carries the
  // legibility. Pittsburgh's #000000 would otherwise be a hole in the tile.
  return lift(color, 0x04070E, 1.6f);
}

lv_color_t badgeInk(uint32_t fill) {
  return contrast(0xFFFFFF, fill) >= contrast(0x04070E, fill) ? lv_color_white()
                                                              : C_PLATE;
}

// ── logo chip solve ────────────────────────────────────────────────────────
//
// See theme.h for why this is a search over grounds rather than a test of the
// mark's brightness. The short version: ink loss does not correlate with mean
// luminance, because it lives in interior keylines an average cannot see.

#define CHIP_BINS 64
#define CHIP_LOST 1.5f      // a pixel below this ratio has effectively vanished

/** Fraction of the histogram's mass that falls below CHIP_LOST on `ground`. */
static float inkLost(const uint32_t* binColor, const uint16_t* binCount,
                     uint32_t total, uint32_t ground) {
  if (!total) return 0.0f;
  uint32_t lost = 0;
  for (int i = 0; i < CHIP_BINS; i++) {
    if (!binCount[i]) continue;
    if (contrast(binColor[i], ground) < CHIP_LOST) lost += binCount[i];
  }
  return (float)lost / (float)total;
}

LogoChip chipSolve(const uint8_t* px, int w, int h, uint32_t against) {
  // Bin by luminance, keeping a representative colour per bin. Evaluating 33
  // candidates against 64 bins is 2,112 comparisons; against every pixel it
  // would be 76,032.
  uint32_t binColor[CHIP_BINS] = { 0 };
  uint16_t binCount[CHIP_BINS] = { 0 };
  uint32_t total = 0;
  uint32_t sumR = 0, sumG = 0, sumB = 0;

  for (int i = 0, n = w * h; i < n; i++) {
    const uint8_t a = px[i * 3 + 2];
    if (a < 128) continue;                       // not ink
    const uint16_t v = (uint16_t)px[i * 3] | ((uint16_t)px[i * 3 + 1] << 8);
    const uint32_t r = (uint32_t)(((v >> 11) & 0x1F) * 255 / 31);
    const uint32_t g = (uint32_t)(((v >> 5) & 0x3F) * 255 / 63);
    const uint32_t b = (uint32_t)((v & 0x1F) * 255 / 31);
    const uint32_t c = (r << 16) | (g << 8) | b;

    const int bin = (int)(relLum(c) * (CHIP_BINS - 1) + 0.5f);
    binColor[bin] = c;                           // last writer wins; representative
    if (binCount[bin] < 0xFFFF) binCount[bin]++;
    total++;
    sumR += r; sumG += g; sumB += b;
  }
  LogoChip out = { 0, 0 };
  if (!total) return out;

  // Baseline: what the mark loses drawn straight on the tile, as today.
  const float base = inkLost(binColor, binCount, total, against);

  (void)sumR; (void)sumG; (void)sumB;

  // ── the objective ────────────────────────────────────────────────────────
  //
  // NOT "minimise ink loss". That objective drives every chip toward white,
  // and a board of white pucks reads as stickers pasted onto the panel.
  //
  // Instead: the DIMMEST ground that gets loss under the threshold. Same rule
  // teamInk() already follows — lift only as far as the requirement, then
  // stop. Most marks need far less than white, so most chips come out dark
  // enough to sit inside the surface family rather than punch out of it.
  //
  // The candidate ramp is neutral only. An earlier version also tried the
  // mark's own averaged colour lifted toward white; it scored the same on ink
  // loss (which is why it looked worth having) and rendered Boston's red on a
  // PINK chip. Averaging a two-tone mark invents a colour the team does not
  // own, and a chip is chrome — it is the one place team colour does not
  // belong.
  // Ink loss is NOT monotonic in ground brightness. A two-tone mark — a dark
  // outline around a light field, which is most crests — is worst against a
  // mid grey and better at both ends, so the ramp has an interior maximum.
  // Scan all of it: 33 candidates against 64 bins is 2,112 comparisons.
  const float kAccept = 0.05f;                 // ink we tolerate losing
  float bestLoss = base;                       // fallback: min loss anywhere
  uint32_t bestColor = 0;
  float dimLoss = 0.0f;                        // first ground clearing kAccept
  uint32_t dimColor = 0;

  for (int step = 0; step <= 32; step++) {
    const uint32_t k = (uint32_t)(step * 255 / 32);
    // Cool-biased neutral, so the chip belongs to the same family as the
    // surfaces around it rather than reading as a foreign grey.
    const uint32_t r = k;
    const uint32_t g = k + (k < 240 ? 6 : 0);
    const uint32_t b = k + (k < 226 ? 14 : 0);
    const uint32_t c = (r << 16) | (g << 8) | b;
    const float l = inkLost(binColor, binCount, total, c);

    if (!dimColor && l <= kAccept) { dimColor = c; dimLoss = l; }
    if (l < bestLoss) { bestLoss = l; bestColor = c; }
  }
  // Prefer the DIMMEST acceptable ground over the outright best one — the
  // difference between a chip that sits in the surface family and a white
  // puck. Fall back to min-loss only when nothing clears the threshold.
  if (dimColor) { bestColor = dimColor; bestLoss = dimLoss; }

  // Only spend an object if it actually buys something. Without this a third
  // of the set gains a chip to recover ~1% of ink, which is visual noise
  // bought with nothing.
  if (bestColor && base - bestLoss > 0.02f) { out.color = bestColor; out.opa = LV_OPA_COVER; }
  return out;
}

// Indices 0..2 match GameState: GS_PRE, GS_LIVE, GS_FINAL. Index 3 is SI_HERO.
//
// Every ink here was SOLVED against its own surface rather than picked, which
// is why the values look arbitrary: each is the first step on a cool-neutral
// ramp that clears its target ratio on that specific fill. ink >= 10:1,
// ink2 >= 7:1, ink3 >= 4.5:1. Re-solve rather than eyeball if a surface moves.
const StateInk kStateInk[4] = {
  // pre — present but recessive
  { lv_color_hex(0x16202E), lv_color_hex(0x2E3A4C),
    lv_color_hex(0xD2DEEA), lv_color_hex(0x9CACBE), lv_color_hex(0x78889A), 0x16202E },
  // live — full strength
  { lv_color_hex(0x1B2636), lv_color_hex(0x3A4759),
    lv_color_hex(0xF3F7FB), lv_color_hex(0xA2B2C4), lv_color_hex(0x7E8EA0), 0x1B2636 },
  // final — quieter, but every tier still reads
  { lv_color_hex(0x101825), lv_color_hex(0x232E3E),
    lv_color_hex(0xB6C4D2), lv_color_hex(0x95A5B7), lv_color_hex(0x728294), 0x101825 },
  // hero — the lightest surface on the panel, so the brightest inks
  { lv_color_hex(0x222E40), lv_color_hex(0x44526A),
    lv_color_hex(0xF3F7FB), lv_color_hex(0xACBCCE), lv_color_hex(0x8696A8), 0x222E40 },
};

void themeInit() {
  buildLinearLut();

  lv_obj_t* scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, C_PLATE, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // The glass primitive. LVGL 8.3 has no backdrop blur and this panel could not
  // afford one, so "frost" is a FLAT fill plus a specular pair.
  //
  // It used to be a vertical gradient. On RGB565 that ramp spans one to two
  // steps of the 5-bit blue channel over 128 px, so it did not render as a
  // gradient at all — it quantised into three flat slabs with two hard
  // horizontal edges, in every tile, at the same two heights. Confirmed by
  // scanning a column: rgb(16,32,49) / rgb(16,32,41) / rgb(16,28,41).
  //
  // Depth now comes from the edges instead, which is where it comes from in
  // real glass anyway: a gradient SIMULATES a light source, a specular edge IS
  // one. Do not reintroduce bg_grad_dir here — if a background ramp is ever
  // wanted it has to be baked and dithered at build time (UI.md §1).
  lv_style_init(&s_glass);
  lv_style_set_bg_color(&s_glass, C_FROST);
  lv_style_set_bg_opa(&s_glass, LV_OPA_COVER);
  lv_style_set_border_color(&s_glass, C_EDGE);
  lv_style_set_border_width(&s_glass, 1);
  lv_style_set_border_opa(&s_glass, LV_OPA_COVER);
  lv_style_set_radius(&s_glass, 12);
  lv_style_set_pad_all(&s_glass, 0);
  lv_style_set_text_color(&s_glass, C_INK);
  lv_style_set_text_font(&s_glass, F_BODY);

  lv_style_init(&s_glassPressed);
  lv_style_set_bg_color(&s_glassPressed, lv_color_hex(0x243040));
  lv_style_set_border_color(&s_glassPressed, C_EDGE_HI);

  lv_style_init(&s_badge);
  lv_style_set_radius(&s_badge, 7);
  lv_style_set_bg_opa(&s_badge, LV_OPA_COVER);
  lv_style_set_border_width(&s_badge, 0);
  lv_style_set_text_color(&s_badge, lv_color_white());
  lv_style_set_text_font(&s_badge, F_MICRO);
  lv_style_set_pad_all(&s_badge, 0);
}

/**
 * Re-lay the specular pair from the panel's CURRENT size.
 *
 * The geometry used to be baked at construction, which is correct for every
 * panel built at its final size and wrong for the one that is not. The board's
 * filler is created as a 10x10 placeholder with radius 12 and resized to ~768
 * once its content is known — so its highlight and shade were laid out as
 * `10 - 2*12` = MINUS FOURTEEN pixels wide, clamped to nothing, and never
 * revisited. The filler is on screen most nights, and it was the only card on
 * the board that did not read as glass.
 *
 * Driven by LV_EVENT_SIZE_CHANGED, so it costs nothing until a panel resizes.
 */
static void glassRelayout(lv_event_t* e) {
  lv_obj_t* o = lv_event_get_target(e);
  lv_obj_t* hi = lv_obj_get_child(o, 0);
  lv_obj_t* lo = lv_obj_get_child(o, 1);
  if (!hi || !lo) return;

  const int w = lv_obj_get_width(o), h = lv_obj_get_height(o);
  // Clamp: a radius wider than the panel is half is not a rounded rect, and
  // the inset must never go negative however the panel is sized.
  int r = lv_obj_get_style_radius(o, LV_PART_MAIN);
  if (r > w / 2) r = w / 2;
  const int sw = w - 2 * r;
  if (sw <= 0 || h <= 6) {
    lv_obj_add_flag(hi, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(lo, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_clear_flag(hi, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(lo, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_size(hi, sw, 1);
  lv_obj_set_pos(hi, r, 0);
  lv_obj_set_size(lo, sw, 2);
  lv_obj_set_pos(lo, r, h - 3);
}

lv_obj_t* glassPanel(lv_obj_t* parent, int x, int y, int w, int h, int radius) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_add_style(o, &s_glass, 0);
  lv_obj_add_style(o, &s_glassPressed, LV_STATE_PRESSED);
  lv_obj_set_style_radius(o, radius, 0);
  lv_obj_set_pos(o, x, y);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_side(o, LV_BORDER_SIDE_FULL, 0);

  // The specular pair — a bright catch along the top and a shade along the
  // bottom. Two children, laid out by glassRelayout(). This asymmetry is what
  // actually reads as glass; a gradient would only band (see the note above).
  //
  // They are children 0 and 1 and glassRelayout() indexes them positionally,
  // so nothing may be inserted ahead of them.
  lv_obj_t* hi = lv_obj_create(o);
  lv_obj_remove_style_all(hi);
  lv_obj_set_style_bg_color(hi, C_LINE, 0);
  lv_obj_set_style_bg_opa(hi, OPA_SPEC, 0);

  lv_obj_t* lo = lv_obj_create(o);
  lv_obj_remove_style_all(lo);
  lv_obj_set_style_bg_color(lo, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(lo, 90, 0);

  lv_obj_add_event_cb(o, glassRelayout, LV_EVENT_SIZE_CHANGED, nullptr);
  lv_obj_set_size(o, w, h);      // fires the callback; must come AFTER the pair
  return o;
}

lv_obj_t* teamBadge(lv_obj_t* parent, const char* abbr, uint32_t color, int size) {
  lv_obj_t* b = lv_obj_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_add_style(b, &s_badge, 0);
  lv_obj_set_size(b, size, size);
  const uint32_t fill = teamFill(color);
  lv_obj_set_style_bg_color(b, lv_color_hex(fill), 0);
  lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* l = lv_label_create(b);
  lv_label_set_text(l, abbr);
  lv_obj_set_style_text_font(l, F_MICRO, 0);
  // White is wrong on a pale kit — Seattle's #99D9D9 carried it at 1.58:1.
  lv_obj_set_style_text_color(l, badgeInk(fill), 0);
  lv_obj_center(l);
  return b;
}

void teamBadgeSet(lv_obj_t* badge, uint32_t color) {
  const uint32_t fill = teamFill(color);
  lv_obj_set_style_bg_color(badge, lv_color_hex(fill), 0);
  lv_obj_t* l = lv_obj_get_child(badge, 0);
  if (l) lv_obj_set_style_text_color(l, badgeInk(fill), 0);
}
