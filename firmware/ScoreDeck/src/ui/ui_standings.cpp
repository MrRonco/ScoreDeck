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
  lv_obj_set_style_bg_color(s_root, C_PLATE, 0);
  lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
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
    s_badge[r] = teamBadge(card, "", 0x5D6D7E, 22);
    lv_obj_set_pos(s_badge[r], 44, y + 3);
    s_badgeLbl[r] = lv_obj_get_child(s_badge[r], 0);
    s_team[r]  = lb(card, 74, y + 6, C_INK, F_MICRO);
    for (uint8_t c = 0; c < ST_MAX_COLS; c++)
      s_cell[r][c] = lb(card, 0, y + 6, C_INK2, F_MICRO, LV_TEXT_ALIGN_RIGHT, 58);

    // A labelled hairline, drawn between rows. No colour spent.
    s_cutRule[r] = lv_obj_create(card);
    lv_obj_remove_style_all(s_cutRule[r]);
    lv_obj_set_size(s_cutRule[r], 560, 1);
    lv_obj_set_pos(s_cutRule[r], 14, y + 33);
    lv_obj_set_style_bg_color(s_cutRule[r], C_EDGE_HI, 0);
    lv_obj_set_style_bg_opa(s_cutRule[r], LV_OPA_COVER, 0);
    lv_obj_add_flag(s_cutRule[r], LV_OBJ_FLAG_HIDDEN);
    s_cutLbl[r] = lb(card, 586, y + 27, C_INK3, F_MICRO, LV_TEXT_ALIGN_RIGHT, 160);
    lv_obj_add_flag(s_cutLbl[r], LV_OBJ_FLAG_HIDDEN);
  }
}

void uiStandingsRender() {
  const Standings& t = g_standings;
  char buf[48];
  snprintf(buf, sizeof buf, "STANDINGS  %s", s_league);
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
    lv_obj_set_style_bg_color(s_badge[r], lv_color_hex(row.color), 0);
    lv_label_set_text(s_badgeLbl[r], row.abbr);
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
      lv_obj_set_pos(s_cell[r][c], colX(c, t.colCount), 34 + r * 36 + 6);
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
