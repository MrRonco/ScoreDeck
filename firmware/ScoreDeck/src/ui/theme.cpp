// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
#include "theme.h"
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

const lv_font_t* F_SCORE = &font_score38;
const lv_font_t* F_SCORE_BIG = &font_score46;
const lv_font_t* F_DISPLAY = &font_display30;
const lv_font_t* F_ABBR  = &font_abbr17;
const lv_font_t* F_BODY  = &font_body15;
const lv_font_t* F_MICRO = &font_micro11;

static lv_style_t s_glass;
static lv_style_t s_badge;

void themeInit() {
  lv_obj_t* scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, C_PLATE, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // The glass primitive. LVGL 8.3 has no backdrop blur and this panel could not
  // afford one, so "frost" is a flat translucent fill over the plate plus the
  // specular top edge. When the pre-blurred plate lands (UI.md §1) only the
  // gradient below changes. Nothing frosted ever moves.
  lv_style_init(&s_glass);
  lv_style_set_bg_color(&s_glass, C_FROST);
  lv_style_set_bg_grad_color(&s_glass, C_FROST_2);
  lv_style_set_bg_grad_dir(&s_glass, LV_GRAD_DIR_VER);
  lv_style_set_bg_opa(&s_glass, LV_OPA_COVER);
  lv_style_set_border_color(&s_glass, C_EDGE);
  lv_style_set_border_width(&s_glass, 1);
  lv_style_set_border_opa(&s_glass, LV_OPA_COVER);
  lv_style_set_radius(&s_glass, 12);
  lv_style_set_pad_all(&s_glass, 0);
  lv_style_set_text_color(&s_glass, C_INK);
  lv_style_set_text_font(&s_glass, F_BODY);

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
  lv_obj_set_style_radius(o, radius, 0);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  // Specular top edge — 1px highlight, the cheap half of the glass read.
  lv_obj_set_style_border_side(o, LV_BORDER_SIDE_FULL, 0);
  return o;
}

lv_obj_t* teamBadge(lv_obj_t* parent, const char* abbr, uint32_t color, int size) {
  lv_obj_t* b = lv_obj_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_add_style(b, &s_badge, 0);
  lv_obj_set_size(b, size, size);
  lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
  lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* l = lv_label_create(b);
  lv_label_set_text(l, abbr);
  lv_obj_set_style_text_font(l, F_MICRO, 0);
  lv_obj_set_style_text_color(l, lv_color_white(), 0);
  lv_obj_center(l);
  return b;
}
