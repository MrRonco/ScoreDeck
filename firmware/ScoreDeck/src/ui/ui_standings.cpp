// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
// ui_standings.cpp — UI.md §6.
//
// The proxy sends a generic {cols, rows} table, so this renders NHL points,
// soccer goal difference and NBA games-behind without knowing which is which.
// Two details do real work: a followed team keeps the edge light it has on the
// board, so you find yourself without reading; and a cut line is a LABELLED
// HAIRLINE rather than a colour band, which keeps the no-accent rule intact
// while saying exactly what the boundary means.
#include "ui.h"
#include "theme.h"
#include "../config.h"
#include "../core/state.h"
#include "../net/api.h"
#include <ctype.h>

#define ST_VIS_ROWS 10

static lv_obj_t* s_root;
static lv_obj_t* s_title;
static lv_obj_t* s_hint;
static lv_obj_t* s_colHdr[ST_MAX_COLS];
static lv_obj_t* s_rank[ST_VIS_ROWS];
static lv_obj_t* s_badge[ST_VIS_ROWS];
static lv_obj_t* s_badgeLbl[ST_VIS_ROWS];
static lv_obj_t* s_team[ST_VIS_ROWS];
static lv_obj_t* s_cell[ST_VIS_ROWS][ST_MAX_COLS];
static lv_obj_t* s_edge[ST_VIS_ROWS];
static lv_obj_t* s_cutRule[ST_VIS_ROWS];
static lv_obj_t* s_cutLbl[ST_VIS_ROWS];

static uint8_t s_scroll;          // first visible row
static char    s_league[8];

lv_obj_t* uiStandingsRoot() { return s_root; }

// Right-aligned numeric columns start after rank + team name.
static int colX(uint8_t i, uint8_t n) {
  const int right = 768 - 14;
  const int w = 62;
  return right - (n - i) * w;
}

static lv_obj_t* lb(lv_obj_t* p, int x, int y, lv_color_t c, const lv_font_t* f,
                    lv_text_align_t a = LV_TEXT_ALIGN_LEFT, int w = 0) {
  lv_obj_t* l = lv_label_create(p);
  lv_obj_set_pos(l, x, y);
  lv_obj_set_style_text_color(l, c, 0);
  lv_obj_set_style_text_font(l, f, 0);
  if (w) { lv_obj_set_width(l, w); lv_obj_set_style_text_align(l, a, 0); }
  lv_label_set_text(l, "");
  return l;
}

static void onBack(lv_event_t*) { uiStandingsClose(); }
static void onGesture(lv_event_t*) {
  const lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_get_act());
  if (d == LV_DIR_TOP && s_scroll + ST_VIS_ROWS < g_standings.rowCount) { s_scroll++; uiStandingsRender(); }
  else if (d == LV_DIR_BOTTOM && s_scroll) { s_scroll--; uiStandingsRender(); }
}

void uiStandingsInit(lv_obj_t* parent) {
  s_root = lv_obj_create(parent);
  lv_obj_remove_style_all(s_root);
  lv_obj_set_size(s_root, SCR_W, SCR_H);
  // Transparent, so the generated plate shows through. Only one screen root
  // is ever unhidden at a time, so nothing needs an opaque root to cover the
  // screen beneath it — and painting a flat C_PLATE here would hide the very
  // thing plate.cpp exists to draw.
  lv_obj_set_style_bg_color(s_root, C_PLATE, 0);
  lv_obj_set_style_bg_opa(s_root, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(s_root, onGesture, LV_EVENT_GESTURE, nullptr);

  lv_obj_t* bar = glassPanel(s_root, 0, 0, SCR_W, BAR_H, 0);
  lv_obj_t* back = lv_btn_create(bar);
  lv_obj_set_size(back, 48, 34);
  lv_obj_set_pos(back, 14, 7);
  lv_obj_set_style_bg_color(back, C_EDGE, 0);
  lv_obj_set_style_border_width(back, 0, 0);
  lv_obj_set_style_radius(back, 8, 0);
  lv_obj_add_event_cb(back, onBack, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* bl = lv_label_create(back);
  lv_label_set_text(bl, "<");
  lv_obj_set_style_text_font(bl, F_BODY, 0);   // F_ABBR has no glyph for "<"
  lv_obj_set_style_text_color(bl, C_INK, 0);
  lv_obj_center(bl);

  s_title = lb(bar, 74, 15, C_INK, F_ABBR);
  s_hint  = lb(bar, SCR_W - 18 - 320, 17, C_INK3, F_MICRO, LV_TEXT_ALIGN_RIGHT, 320);

  lv_obj_t* card = glassPanel(s_root, 16, 60, 768, 404, 12);

  for (uint8_t c = 0; c < ST_MAX_COLS; c++)
    s_colHdr[c] = lb(card, 0, 10, C_INK3, F_MICRO, LV_TEXT_ALIGN_RIGHT, 58);

  for (uint8_t r = 0; r < ST_VIS_ROWS; r++) {
    const int y = 34 + r * 36;
    s_edge[r] = lv_obj_create(card);
    lv_obj_remove_style_all(s_edge[r]);
    lv_obj_set_size(s_edge[r], 3, 26);
    lv_obj_set_pos(s_edge[r], 0, y + 2);
    lv_obj_set_style_bg_opa(s_edge[r], LV_OPA_COVER, 0);
    lv_obj_add_flag(s_edge[r], LV_OBJ_FLAG_HIDDEN);

    s_rank[r]  = lb(card, 14, y + 6, C_INK3, F_MICRO, LV_TEXT_ALIGN_RIGHT, 22);
    // 30, not 22. badgeLabelFit() allows (size-6)*16/125 glyphs, so a 22 px chip
    // holds TWO — and every NHL/NFL/MLB abbreviation is three. F_MICRO is mono at
    // 7.8125 px, so "FLA" measures 23.44 px inside a 22 px box: wider than its own
    // container before the R_SM corners cut into it. This is the one screen where
    // the chip IS the team — there is no logo blob and no city name beside it — so
    // the fix grows the chip rather than truncating the mark. The 36 px row pitch
    // has the room: 30 leaves 3 px above and a clear pixel below the cut rule.
    s_badge[r] = teamBadge(card, "", 0x5D6D7E, 30);
    lv_obj_set_pos(s_badge[r], 44, y + 3);
    s_badgeLbl[r] = lv_obj_get_child(s_badge[r], 0);
    // Team names are upstream prose — Munchen, Atletico, Besiktas.
    s_team[r]  = lb(card, 80, y + 5, C_INK, F_BODY);
    for (uint8_t c = 0; c < ST_MAX_COLS; c++)
      s_cell[r][c] = lb(card, 0, y + 5, C_INK2, F_NUM, LV_TEXT_ALIGN_RIGHT, 58);

    // A labelled hairline, drawn between rows. No colour spent.
    s_cutRule[r] = lv_obj_create(card);
    lv_obj_remove_style_all(s_cutRule[r]);
    lv_obj_set_size(s_cutRule[r], 740, 1);
    lv_obj_set_pos(s_cutRule[r], 14, y + 33);
    lv_obj_set_style_bg_color(s_cutRule[r], C_EDGE_HI, 0);
    lv_obj_set_style_bg_opa(s_cutRule[r], LV_OPA_COVER, 0);
    lv_obj_add_flag(s_cutRule[r], LV_OBJ_FLAG_HIDDEN);
    // Placed in the one band of the table that is empty on every row: team
    // names end near x=200 and the first stat column starts near x=490.
    //
    // Two earlier positions were both wrong for the same reason — they sat in
    // space that belongs to something else. Right-aligned at x=594 it landed
    // directly under the PTS value and read as a caption for that number
    // ("86 — PLAYOFF LINE"). Moved to x=14 it collided with the NEXT row's
    // rank and badge, because the 36 px pitch leaves only 7 px of true gap and
    // a 13 px label cannot be centred in it.
    //
    // It carries the card's own fill so it masks the rule it sits on, rather
    // than needing the rule split into two objects around it.
    s_cutLbl[r] = lb(card, 290, y + 26, C_INK3, F_MICRO, LV_TEXT_ALIGN_LEFT, 200);
    lv_obj_set_style_text_letter_space(s_cutLbl[r], 1, 0);
    lv_obj_set_style_bg_color(s_cutLbl[r], C_FROST, 0);
    lv_obj_set_style_bg_opa(s_cutLbl[r], LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(s_cutLbl[r], 8, 0);
    lv_obj_add_flag(s_cutLbl[r], LV_OBJ_FLAG_HIDDEN);
  }
}

void uiStandingsRender() {
  const Standings& t = g_standings;
  char buf[48];
  // League slugs are lowercase ("nhl", "eng.1") and the title face is caps-only,
  // so the slug rendered as boxes. Upper-case it rather than change the face —
  // the title reads as a caps label by design.
  char slug[sizeof s_league];
  for (size_t i = 0; i < sizeof slug; i++) slug[i] = (char)toupper((unsigned char)s_league[i]);
  slug[sizeof slug - 1] = '\0';
  snprintf(buf, sizeof buf, "STANDINGS  %s", slug);
  lv_label_set_text(s_title, buf);

  if (!t.rowCount) {
    lv_label_set_text(s_hint, t.loading ? "loading" : "no standings for this league");
    for (uint8_t r = 0; r < ST_VIS_ROWS; r++) {
      lv_label_set_text(s_rank[r], ""); lv_label_set_text(s_team[r], "");
      lv_obj_add_flag(s_badge[r], LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(s_edge[r], LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(s_cutRule[r], LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(s_cutLbl[r], LV_OBJ_FLAG_HIDDEN);
      for (uint8_t c = 0; c < ST_MAX_COLS; c++) lv_label_set_text(s_cell[r][c], "");
    }
    return;
  }

  snprintf(buf, sizeof buf, "%u teams", t.rowCount);
  lv_label_set_text(s_hint, buf);

  for (uint8_t c = 0; c < ST_MAX_COLS; c++) {
    const bool on = c < t.colCount;
    lv_obj_set_pos(s_colHdr[c], colX(c, t.colCount), 10);
    lv_label_set_text(s_colHdr[c], on ? t.cols[c] : "");
  }

  for (uint8_t r = 0; r < ST_VIS_ROWS; r++) {
    const uint8_t idx = s_scroll + r;
    if (idx >= t.rowCount) {
      lv_label_set_text(s_rank[r], ""); lv_label_set_text(s_team[r], "");
      lv_obj_add_flag(s_badge[r], LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(s_edge[r], LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(s_cutRule[r], LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(s_cutLbl[r], LV_OBJ_FLAG_HIDDEN);
      for (uint8_t c = 0; c < ST_MAX_COLS; c++) lv_label_set_text(s_cell[r][c], "");
      continue;
    }
    const StandingRow& row = t.rows[idx];
    char n[6];
    snprintf(n, sizeof n, "%u", idx + 1);
    lv_label_set_text(s_rank[r], n);
    lv_obj_clear_flag(s_badge[r], LV_OBJ_FLAG_HIDDEN);
    teamBadgeSet(s_badge[r], row.color);
    // Through the guard, never raw: badgeLabelFit() is what keeps a mark inside
    // its chip, and this was one of four label sites that bypassed it.
    char fit[6];
    badgeLabelFit(fit, sizeof fit, row.abbr, 30);
    lv_label_set_text(s_badgeLbl[r], fit);
    lv_label_set_text(s_team[r], row.name);

    // A followed team keeps the edge light it has on the board.
    const bool fav = boardFollows(s_league, row.abbr);
    if (fav) {
      lv_obj_clear_flag(s_edge[r], LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_style_bg_color(s_edge[r], lv_color_hex(row.color), 0);
    } else {
      lv_obj_add_flag(s_edge[r], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_text_color(s_team[r], fav ? C_INK : C_INK2, 0);

    for (uint8_t c = 0; c < ST_MAX_COLS; c++) {
      const bool on = c < t.colCount;
      lv_obj_set_pos(s_cell[r][c], colX(c, t.colCount), 34 + r * 36 + 5);
      lv_label_set_text(s_cell[r][c], on ? row.cells[c] : "");
      lv_obj_set_style_text_color(s_cell[r][c], (on && c == t.colCount - 1) ? C_INK : C_INK2, 0);
    }

    const char* cut = nullptr;
    for (uint8_t k = 0; k < t.cutCount; k++)
      if (t.cutAfter[k] == idx) cut = t.cutLabel[k];
    if (cut) {
      lv_obj_clear_flag(s_cutRule[r], LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(s_cutLbl[r], LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(s_cutLbl[r], cut);
    } else {
      lv_obj_add_flag(s_cutRule[r], LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(s_cutLbl[r], LV_OBJ_FLAG_HIDDEN);
    }
  }
}

void uiStandingsOpen(const char* league) {
  strncpy(s_league, league, sizeof s_league - 1);
  s_league[sizeof s_league - 1] = '\0';
  s_scroll = 0;
  g_standings.rowCount = 0;
  g_standings.loading = true;
  uiShow(SCR_STANDINGS);
  uiStandingsRender();
  apiStandingsStart(s_league);
}

void uiStandingsClose() {
  uiShow(uiShouldIdle() ? SCR_IDLE : SCR_BOARD);
}

bool uiStandingsIsOpen() { return uiCurrent() == SCR_STANDINGS; }
