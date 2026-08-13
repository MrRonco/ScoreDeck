// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
// ui_ledger.cpp — settled and scheduled games, as rows on the bare plate.
//
// WHY THERE IS NO CARD HERE. This started as a card, and the card was measured
// and thrown away. The first draft of the featured layout put the hero in a
// 508x268 cell and the ledger in a 768x124 one, and the result covered MORE of
// the panel than the nine-up grid it replaced: 86.8% against 84.4%. It fixed
// the hierarchy and left the diagnosed problem — no figure, no ground —
// completely untouched.
//
// A ledger is a list, not an object. Three rows under a hairline, sitting
// directly on the plate. That single change takes card coverage to 62% and is
// the reason the plate finally reads as a background rather than as grout.
//
// Do not give this a fill, a border or a radius. The hairline and the column
// gap are the whole structure, and they are enough.
#include "ui.h"
#include "theme.h"
#include "../config.h"
#include "../core/state.h"

#define LED_ROWS    3
#define LED_COL_W 348
#define LED_X0     24
#define LED_X1    412
#define LED_ROW_H  30

static lv_obj_t* s_root;
static lv_obj_t* s_hdr[2];
static lv_obj_t* s_rule[2];
static lv_obj_t* s_cell[2][LED_ROWS][3];

// Column 0 is UPCOMING, column 1 is FINAL. Each row is three fields; the
// widths differ per column because a start time and a final score are not the
// same shape of thing.
// Field 0 of UPCOMING carries a start time, and 50 px is not one: "7:00 PM" is
// seven mono glyphs at roughly 9 px, so it ellipsised to "7:..." — the field
// clipped exactly the part that made it a time.
static const int kColX[2][3] = { { 0, 78, 214 }, { 0, 96, 214 } };
static const int kColW[2][3] = { { 74, 130, 134 }, { 92, 112, 134 } };

static lv_obj_t* cell(lv_obj_t* p, int x, int y, int w, lv_color_t c,
                      lv_text_align_t al = LV_TEXT_ALIGN_LEFT) {
  lv_obj_t* l = lv_label_create(p);
  lv_obj_set_pos(l, x, y);
  lv_obj_set_width(l, w);
  lv_obj_set_style_text_color(l, c, 0);
  lv_obj_set_style_text_font(l, F_NUM, 0);
  lv_obj_set_style_text_align(l, al, 0);
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_label_set_text(l, "");
  return l;
}

void uiLedgerInit(lv_obj_t* parent) {
  // A plain container, not a glassPanel: transparent, no border, no radius.
  s_root = lv_obj_create(parent);
  lv_obj_remove_style_all(s_root);
  lv_obj_set_pos(s_root, 0, 340);
  lv_obj_set_size(s_root, SCR_W, SCR_H - 340);
  lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

  static const char* kHdr[2] = { "UPCOMING", "FINAL" };
  for (int c = 0; c < 2; c++) {
    const int x = c ? LED_X1 : LED_X0;

    s_hdr[c] = lv_label_create(s_root);
    lv_obj_set_pos(s_hdr[c], x, 8);
    lv_obj_set_style_text_color(s_hdr[c], C_INK3, 0);
    lv_obj_set_style_text_font(s_hdr[c], F_MICRO, 0);
    // Tracked caps means "this LABELS something"; zero tracking means "this is
    // the data". Two free axes the grid spends neither of.
    lv_obj_set_style_text_letter_space(s_hdr[c], 1, 0);
    lv_label_set_text(s_hdr[c], kHdr[c]);

    s_rule[c] = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_rule[c]);
    lv_obj_set_pos(s_rule[c], x, 28);
    lv_obj_set_size(s_rule[c], LED_COL_W, 1);
    lv_obj_set_style_bg_color(s_rule[c], C_LINE, 0);
    lv_obj_set_style_bg_opa(s_rule[c], OPA_HAIR, 0);

    for (int r = 0; r < LED_ROWS; r++) {
      const int y = 44 + r * LED_ROW_H;
      for (int f = 0; f < 3; f++) {
        const lv_color_t ink = (f == 1) ? C_INK2 : C_INK3;
        const lv_text_align_t al = (f == 2) ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT;
        s_cell[c][r][f] = cell(s_root, x + kColX[c][f], y, kColW[c][f], ink, al);
      }
    }
  }
  lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t* uiLedgerRoot() { return s_root; }

/**
 * Two column layouts: bare-plate at 24/412 with 348-wide columns when the
 * rail is closed, and 156/482 with 300-wide columns when it is open — the
 * rail owns x<140 and the old left column would vanish underneath it.
 * Field widths compress with the column; every cell already carries
 * LV_LABEL_LONG_DOT, so the loss is an ellipsis, not an overlap.
 */
void uiLedgerLayout(bool railOpen) {
  if (!s_root) return;
  static const int xO[2] = { 156, 482 };
  static const int xC[2] = { 24, 412 };
  static const int colXO[2][3] = { { 0, 72, 192 }, { 0, 90, 192 } };
  static const int colWO[2][3] = { { 68, 116, 108 }, { 86, 98, 108 } };
  const int colW = railOpen ? 300 : LED_COL_W;
  for (int c = 0; c < 2; c++) {
    const int x = railOpen ? xO[c] : xC[c];
    lv_obj_set_x(s_hdr[c], x);
    lv_obj_set_pos(s_rule[c], x, 28);
    lv_obj_set_width(s_rule[c], colW);
    for (int r = 0; r < LED_ROWS; r++)
      for (int f = 0; f < 3; f++) {
        const int cx = railOpen ? colXO[c][f] : kColX[c][f];
        const int cw = railOpen ? colWO[c][f] : kColW[c][f];
        lv_obj_set_x(s_cell[c][r][f], x + cx);
        lv_obj_set_width(s_cell[c][r][f], cw);
      }
  }
}
void uiLedgerHide() { if (s_root) lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN); }

/**
 * Fill from g_board in `order`, skipping anything already drawn elsewhere.
 *
 * `exclude` is a small array of game indices the caller has put on the hero or
 * on a tile. A game must never appear twice on one screen — that reads as two
 * fixtures, not as one shown twice.
 */
void uiLedgerRender(const uint8_t* order, uint8_t n,
                    const int8_t* exclude, uint8_t nExclude,
                    bool (*passes)(const Game&)) {
  if (!s_root) return;

  char buf[24];
  int fill[2] = { 0, 0 };

  for (uint8_t oi = 0; oi < n; oi++) {
    const uint8_t gi = order[oi];
    const Game& g = g_board[gi];
    if (passes && !passes(g)) continue;

    bool skip = false;
    for (uint8_t e = 0; e < nExclude; e++) if (exclude[e] == (int8_t)gi) { skip = true; break; }
    if (skip) continue;

    const int c = (g.state == GS_FINAL) ? 1 : (g.state == GS_PRE ? 0 : -1);
    if (c < 0 || fill[c] >= LED_ROWS) continue;      // live games are never here
    const int r = fill[c]++;

    if (c == 0) {
      lv_label_set_text(s_cell[0][r][0], g.status);           // "7:00 PM"
      snprintf(buf, sizeof buf, "%s @ %s", g.away.abbr, g.home.abbr);
      lv_label_set_text(s_cell[0][r][1], buf);
      lv_label_set_text(s_cell[0][r][2], g.bcast);
    } else {
      snprintf(buf, sizeof buf, "%-4s %2u", g.away.abbr, g.away.score);
      lv_label_set_text(s_cell[1][r][0], buf);
      snprintf(buf, sizeof buf, "%-4s %2u", g.home.abbr, g.home.score);
      lv_label_set_text(s_cell[1][r][1], buf);
      lv_label_set_text(s_cell[1][r][2], g.status);           // "Final" | "F/OT"
      // Winner bright, loser recessive — a results list you read without
      // parsing eight numbers. Ties (soccer) keep both sides equal.
      const bool homeWon = g.home.score > g.away.score;
      const bool tie = g.home.score == g.away.score;
      lv_obj_set_style_text_color(s_cell[1][r][0], tie ? C_INK2 : (homeWon ? C_INK3 : C_INK), 0);
      lv_obj_set_style_text_color(s_cell[1][r][1], tie ? C_INK2 : (homeWon ? C_INK : C_INK3), 0);
    }
  }

  for (int c = 0; c < 2; c++) {
    for (int r = fill[c]; r < LED_ROWS; r++)
      for (int f = 0; f < 3; f++) lv_label_set_text(s_cell[c][r][f], "");
    // An empty column keeps its header only if it has something under it — a
    // heading over nothing is the same "failed to load" signal a bordered
    // empty card gives, in a cheaper form.
    const lv_opa_t o = fill[c] ? LV_OPA_COVER : LV_OPA_TRANSP;
    lv_obj_set_style_opa(s_hdr[c], o, 0);
    lv_obj_set_style_opa(s_rule[c], o, 0);
  }

  if (!fill[0] && !fill[1]) { lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN); return; }
  lv_obj_clear_flag(s_root, LV_OBJ_FLAG_HIDDEN);
}
