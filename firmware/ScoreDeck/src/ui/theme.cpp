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

const lv_font_t* F_SCORE = &font_score38;
const lv_font_t* F_SCORE_BIG = &font_score46;
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

  int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
  const int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
  if (mx > 0) {                                        // stage 1: pure value lift
    r = r * 255 / mx; g = g * 255 / mx; b = b * 255 / mx;
    const uint32_t sat = (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
    if (contrast(sat, against) >= minRatio) return sat;
  }
  for (int t = 1; t <= 100; t++) {                     // stage 2: whiten residual
    const uint32_t out = (uint32_t)(r + (255 - r) * t / 100) << 16 |
                         (uint32_t)(g + (255 - g) * t / 100) << 8 |
                         (uint32_t)(b + (255 - b) * t / 100);
    if (contrast(out, against) >= minRatio) return out;
  }
  return 0xFFFFFF;
}

uint32_t teamInk(uint32_t color) {
  return lift(color, 0x101C29, 3.5f);      // against the tile fill
}

uint32_t teamFill(uint32_t color) {
  // A badge only has to separate from the plate; its own label carries the
  // legibility. Pittsburgh's #000000 would otherwise be a hole in the tile.
  return lift(color, 0x0A0F18, 1.6f);
}

lv_color_t badgeInk(uint32_t fill) {
  return contrast(0xFFFFFF, fill) >= contrast(0x0A0F18, fill) ? lv_color_white()
                                                              : C_PLATE;
}

// Order matches GameState: GS_PRE, GS_LIVE, GS_FINAL.
const StateInk kStateInk[3] = {
  // pre — present but recessive
  { lv_color_hex(0x151D29), lv_color_hex(0x263243),
    lv_color_hex(0xC9D6E2), lv_color_hex(0x8494A6), lv_color_hex(0x6C7C8D) },
  // live — full strength
  { lv_color_hex(0x101C29), lv_color_hex(0x2A3646),
    lv_color_hex(0xF3F7FB), lv_color_hex(0x93A5B8), lv_color_hex(0x7A8899) },
  // final — quieter, but every tier still reads
  { lv_color_hex(0x0E141D), lv_color_hex(0x1F2937),
    lv_color_hex(0xA8B6C4), lv_color_hex(0x7C8B9C), lv_color_hex(0x667484) },
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

lv_obj_t* glassPanel(lv_obj_t* parent, int x, int y, int w, int h, int radius) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_add_style(o, &s_glass, 0);
  lv_obj_add_style(o, &s_glassPressed, LV_STATE_PRESSED);
  lv_obj_set_style_radius(o, radius, 0);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_side(o, LV_BORDER_SIDE_FULL, 0);

  // The specular pair — a bright catch along the top and a shade along the
  // bottom. Two static children, drawn once, no per-frame cost. This asymmetry
  // is what actually reads as glass; the file used to claim it and not do it.
  if (h > 6) {
    lv_obj_t* hi = lv_obj_create(o);
    lv_obj_remove_style_all(hi);
    lv_obj_set_size(hi, w - 2 * radius, 1);
    lv_obj_set_pos(hi, radius, 0);
    lv_obj_set_style_bg_color(hi, C_EDGE_HI, 0);
    lv_obj_set_style_bg_opa(hi, 210, 0);

    lv_obj_t* lo = lv_obj_create(o);
    lv_obj_remove_style_all(lo);
    lv_obj_set_size(lo, w - 2 * radius, 2);
    lv_obj_set_pos(lo, radius, h - 3);
    lv_obj_set_style_bg_color(lo, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(lo, 90, 0);
  }
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
