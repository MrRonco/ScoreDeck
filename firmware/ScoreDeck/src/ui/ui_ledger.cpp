// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
// ui_ledger.cpp — the results row under the hero. UI.md §5.2.
//
// HISTORY, because this file has now been both things and the reasons matter.
//
// It began as a CARD and the card was measured and thrown away: the first
// featured layout put the hero in a 508x268 cell and the ledger in one
// 768x124 slab, and the result covered MORE of the panel than the nine-up
// grid it replaced — 86.8% against 84.4%. It fixed the hierarchy and left the
// diagnosed problem, no figure and no ground, completely untouched. So it
// became a bare list on the plate, which took card coverage to 62% and is the
// reason the plate finally reads as a background rather than as grout.
//
// The list then had three problems of its own, all visible at once:
//
//   * it aligned with nothing. Its columns broke at 16/412 while the tile
//     column above ended at 784, so the lower band missed the grid overhead by
//     124-136 px and the screen read as two unrelated layouts.
//   * the lower LEFT was usually dead. UPCOMING only has content while
//     fixtures remain; after the last first pitch it is empty for the rest of
//     the night, and the band reserved half the width for it regardless.
//     Measured on a real frame: 376x136 px containing ZERO lit pixels.
//   * it showed the WRONG finals. The board's order is start-time ASCENDING
//     and this file took the first three finals it met, so on any night with
//     more than three completed games it locked onto the games that finished
//     EARLIEST and the one that had just ended never appeared at all.
//
// So it is a ROW OF CARDS now — but deliberately NOT the slab that was
// rejected. The old lesson was that one 768-wide fill smothers the ground;
// this is three cards on the SAME x grid as the tiles above, with plate
// showing in both gutters and a 40 px plate reserve beneath. The bottom band
// carries 74,400 px of card against the rejected version's 95,232 — 22% less
// — and total card coverage lands at ~71%, between the bare list (52%) and
// the grid (84.4%). If it ever creeps back toward the slab, RES_H is the
// constant that did it: 100 px is the budget, 128 px reproduces the rejection.
//
// Content is one stream, not two fixed bins: finished games claim slots
// newest-first, upcoming fixtures fill whatever is left soonest-first. A quiet
// night shows one card, not one card and a hole.
#include "ui.h"
#include "theme.h"
#include "../config.h"
#include "../core/state.h"
#include <string.h>
#include <stdio.h>

#define RES_MAX  3          // one grid row
#define RES_Y  340
#define RES_H  100          // the coverage budget — see the header

static lv_obj_t* s_root;
static lv_obj_t* s_card[RES_MAX];
static lv_obj_t* s_abbr[RES_MAX][2];
static lv_obj_t* s_score[RES_MAX][2];
static lv_obj_t* s_meta[RES_MAX];
static lv_obj_t* s_bcast[RES_MAX];
// See ui_hero.cpp: lv_obj_clear_flag invalidates unconditionally, and this
// root is 800x140. Its own visibility is change-cached.
static bool      s_rootVis;
static int8_t    s_k = RES_MAX;        // how many cards the last render filled
static int8_t    s_cardVis[RES_MAX];   // -1 unset, else 0/1 — same discipline

// ── the rest of the change cache ───────────────────────────────────────────
// The visibility half above shipped; the CONTENT half never did, so every poll
// rewrote all three cards whether or not a byte had moved — 144,004 px on an
// identical re-apply, measured with --measure poll --scenario 10.
//
// At the 60 s default poll that is 45 ms of flush per minute and nothing is
// visible at 610 mm (§4 item 24 corrects the audit on exactly this point). It
// is worth doing anyway because it is not only the poll: the boot burst fires
// one refresh per logo arrival, ~18-24 of them, and the page-flip and filter
// paths run the same function inside a touch response, where 45 ms lands.
static char      s_cAbbr [RES_MAX][2][8];
static char      s_cScore[RES_MAX][2][8];
static char      s_cMeta [RES_MAX][20];
static char      s_cBcast[RES_MAX][16];
// Ink is cached by a CODE, never by the colour value: lv_color_t is RGB565
// here, and round-tripping a solved state ink through a uint32 would quietly
// shift it. Same reason ui_board.cpp:1417 uses sentinels rather than hexes.
static uint8_t   s_cPlate[RES_MAX];
static uint8_t   s_cInk  [RES_MAX][2];
static uint8_t   s_cMetaInk[RES_MAX];
static uint8_t   s_cBcastInk[RES_MAX];

/** 0 means "nothing written yet"; otherwise state and role, packed. */
static inline uint8_t inkCode(GameState st, uint8_t role) {
  return (uint8_t)(1 + (uint8_t)st * 4 + role);
}
static void setInkCached(lv_obj_t* o, uint8_t* cache, uint8_t code, lv_color_t c) {
  if (*cache == code) return;
  *cache = code;
  lv_obj_set_style_text_color(o, c, 0);
}
static void setPlateCached(lv_obj_t* o, uint8_t* cache, uint8_t code, lv_color_t c) {
  if (*cache == code) return;
  *cache = code;
  lv_obj_set_style_bg_color(o, c, 0);
}

static lv_obj_t* lab(lv_obj_t* p, const lv_font_t* f, lv_color_t c,
                     lv_text_align_t al = LV_TEXT_ALIGN_LEFT) {
  lv_obj_t* l = lv_label_create(p);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, c, 0);
  lv_obj_set_style_text_align(l, al, 0);
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_label_set_text(l, "");
  return l;
}

void uiLedgerInit(lv_obj_t* parent) {
  s_root = lv_obj_create(parent);
  lv_obj_remove_style_all(s_root);
  lv_obj_set_pos(s_root, 0, RES_Y);
  lv_obj_set_size(s_root, SCR_W, SCR_H - RES_Y);
  lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

  for (int i = 0; i < RES_MAX; i++) {
    // The SAME primitive the live tiles are built from — which is the point:
    // a finished game should look like a game, not like a spreadsheet row.
    s_card[i] = glassPanel(s_root, 0, 0, 248, RES_H, R_LG);
    lv_obj_clear_flag(s_card[i], LV_OBJ_FLAG_SCROLLABLE);

    for (int k = 0; k < 2; k++) {
      s_abbr[i][k]  = lab(s_card[i], F_TITLE, C_INK);
      s_score[i][k] = lab(s_card[i], F_DISPLAY, C_INK, LV_TEXT_ALIGN_RIGHT);
    }
    s_meta[i]  = lab(s_card[i], F_MICRO, C_INK3);
    lv_obj_set_style_text_letter_space(s_meta[i], 1, 0);
    s_bcast[i] = lab(s_card[i], F_MICRO, C_INK3, LV_TEXT_ALIGN_RIGHT);
    lv_obj_add_flag(s_card[i], LV_OBJ_FLAG_HIDDEN);
    s_cardVis[i] = 0;
  }
  // lab() writes "" into every label, so the text caches start matching what
  // is on screen rather than at some value the first render would skip.
  memset(s_cAbbr, 0, sizeof s_cAbbr);
  memset(s_cScore, 0, sizeof s_cScore);
  memset(s_cMeta, 0, sizeof s_cMeta);
  memset(s_cBcast, 0, sizeof s_cBcast);
  memset(s_cPlate, 0, sizeof s_cPlate);
  memset(s_cInk, 0, sizeof s_cInk);
  memset(s_cMetaInk, 0, sizeof s_cMetaInk);
  memset(s_cBcastInk, 0, sizeof s_cBcastInk);
  lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
  s_rootVis = false;
}

lv_obj_t* uiLedgerRoot() { return s_root; }
/** How many result cards the last render actually filled. */
int uiLedgerCount() { return s_k < 0 ? 0 : s_k; }

/**
 * Lay the row on the SAME x grid as the tiles above it, so the two cannot
 * drift apart when the rail opens.
 *
 *   rail closed:  16 / 276 / 536, each 248 wide  (= the Standard grid)
 *   rail open:   156 / 376 / 596, 210 / 210 / 192  (= the narrowed grid)
 */
void uiLedgerLayout(bool railOpen) { uiLedgerLayoutK(railOpen, s_k); }

/**
 * ...and CENTRED when there are fewer than three of them.
 *
 * PLAN item 4.3 costed this ("row re-centred at k<3") and it never shipped:
 * the row laid a fixed xC[] = {16,276,536} and simply hid the unused cards, so
 * a one-result night left a single card pinned to the bottom-left corner with
 * 508 x 100 = 50,800 px of empty plate beside it. FEATURE is chosen precisely
 * when live is 1..3 — the quiet-evening case — so this is the layout's normal
 * condition, not an edge case.
 *
 * The card WIDTH is invariant at every k. Widening the cards to fill the row
 * would break the one-tileW assumption the whole grid derives from; the row
 * keeps the grid's column pitch and moves as a block.
 */
void uiLedgerLayoutK(bool railOpen, int k) {
  if (!s_root) return;
  if (k < 0) k = 0;
  if (k > RES_MAX) k = RES_MAX;
  static const int wC[RES_MAX] = { 248, 248, 248 };
  static const int wO[RES_MAX] = { 210, 210, 192 };
  const int GUT = 12;
  // The band the row lives in: the full frame, or what the rail leaves.
  const int regX = railOpen ? 156 : 16;
  const int regW = 784 - regX;

  int used = 0;
  for (int i = 0; i < k; i++) used += (railOpen ? wO[i] : wC[i]) + (i ? GUT : 0);
  const int x0 = regX + (k >= RES_MAX ? 0 : (regW - used) / 2);

  int x = x0;
  for (int i = 0; i < RES_MAX; i++) {
    const int w = railOpen ? wO[i] : wC[i];
    lv_obj_set_pos(s_card[i], x, 0);
    lv_obj_set_size(s_card[i], w, RES_H);
    x += w + GUT;

    const int pad = 14, scoreW = 62;
    for (int k2 = 0; k2 < 2; k2++) {
      const int y = 8 + k2 * 30;
      lv_obj_set_pos(s_abbr[i][k2], pad, y + 5);
      lv_obj_set_width(s_abbr[i][k2], w - 2 * pad - scoreW - 6);
      lv_obj_set_pos(s_score[i][k2], w - pad - scoreW, y);
      lv_obj_set_width(s_score[i][k2], scoreW);
    }
    lv_obj_set_pos(s_meta[i], pad, RES_H - 24);
    lv_obj_set_width(s_meta[i], w - 2 * pad - 74);
    lv_obj_set_pos(s_bcast[i], w - pad - 74, RES_H - 24);
    lv_obj_set_width(s_bcast[i], 74);
  }
}

void uiLedgerHide() {
  if (s_root && s_rootVis) { s_rootVis = false; lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN); }
}

static void showCard(int i, bool on) {
  if (s_cardVis[i] == (int8_t)on) return;      // add/clear_flag always invalidates
  s_cardVis[i] = (int8_t)on;
  on ? lv_obj_clear_flag(s_card[i], LV_OBJ_FLAG_HIDDEN)
     : lv_obj_add_flag(s_card[i], LV_OBJ_FLAG_HIDDEN);
}

/**
 * Fill from g_board, skipping anything already drawn elsewhere.
 *
 * `exclude` is a small array of game indices the caller has put on the hero or
 * on a tile. A game must never appear twice on one screen — that reads as two
 * fixtures, not as one shown twice.
 */
void uiLedgerRender(const uint8_t* order, uint8_t n,
                    const int8_t* exclude, uint8_t nExclude,
                    bool (*passes)(const Game&)) {
  if (!s_root) return;

  // ── choose, then draw ────────────────────────────────────────────────────
  // Finals NEWEST first: the game that just ended is the one worth a slot, and
  // board order gave the exact opposite. Upcoming soonest-first fills the tail.
  int8_t fin[RES_MAX], pre[RES_MAX];
  uint8_t nFin = 0, nPre = 0;

  for (uint8_t oi = 0; oi < n; oi++) {
    const uint8_t gi = order[oi];
    const Game& g = g_board[gi];
    if (passes && !passes(g)) continue;
    bool skip = false;
    for (uint8_t e = 0; e < nExclude; e++) if (exclude[e] == (int8_t)gi) { skip = true; break; }
    if (skip) continue;

    if (g.state == GS_FINAL) {
      uint8_t p = 0;
      while (p < nFin && g_board[fin[p]].startUtc >= g.startUtc) p++;
      if (p >= RES_MAX) continue;
      for (int8_t q = (int8_t)((nFin < RES_MAX ? nFin : RES_MAX - 1)); q > (int8_t)p; q--)
        fin[q] = fin[q - 1];
      fin[p] = (int8_t)gi;
      if (nFin < RES_MAX) nFin++;
    } else if (g.state == GS_PRE) {
      uint8_t p = 0;
      while (p < nPre && g_board[pre[p]].startUtc <= g.startUtc) p++;
      if (p >= RES_MAX) continue;
      for (int8_t q = (int8_t)((nPre < RES_MAX ? nPre : RES_MAX - 1)); q > (int8_t)p; q--)
        pre[q] = pre[q - 1];
      pre[p] = (int8_t)gi;
      if (nPre < RES_MAX) nPre++;
    }
  }

  uint8_t slot = 0;
  char buf[16];

  for (uint8_t i = 0; i < nFin && slot < RES_MAX; i++, slot++) {
    const Game& g = g_board[fin[i]];
    const StateInk& si = kStateInk[GS_FINAL];
    setPlateCached(s_card[slot], &s_cPlate[slot], inkCode(GS_FINAL, 0), si.plate);
    setTextCached(s_abbr[slot][0], s_cAbbr[slot][0], sizeof s_cAbbr[slot][0], g.away.abbr);
    setTextCached(s_abbr[slot][1], s_cAbbr[slot][1], sizeof s_cAbbr[slot][1], g.home.abbr);
    snprintf(buf, sizeof buf, "%u", (unsigned)g.away.score);
    setTextCached(s_score[slot][0], s_cScore[slot][0], sizeof s_cScore[slot][0], buf);
    snprintf(buf, sizeof buf, "%u", (unsigned)g.home.score);
    setTextCached(s_score[slot][1], s_cScore[slot][1], sizeof s_cScore[slot][1], buf);
    // Winner bright, loser recessive — the tiles' own rule, so a result reads
    // without parsing both numbers. Ties keep both sides equal.
    const bool homeWon = g.home.score > g.away.score;
    const bool tie = g.home.score == g.away.score;
    for (int k = 0; k < 2; k++) {
      const bool won = tie ? true : ((k == 1) == homeWon);
      const lv_color_t c = won ? si.ink : si.ink3;
      const uint8_t code = inkCode(GS_FINAL, won ? 1 : 2);
      // ONE cache for the pair: the abbreviation and the score on a side are
      // written from the same decision and can never disagree.
      if (s_cInk[slot][k] != code) {
        s_cInk[slot][k] = code;
        lv_obj_set_style_text_color(s_abbr[slot][k],  c, 0);
        lv_obj_set_style_text_color(s_score[slot][k], c, 0);
      }
    }
    setTextCached(s_meta[slot], s_cMeta[slot], sizeof s_cMeta[slot], g.status);
    setInkCached(s_meta[slot], &s_cMetaInk[slot], inkCode(GS_FINAL, 3), si.ink3);
    setTextCached(s_bcast[slot], s_cBcast[slot], sizeof s_cBcast[slot], "");
    showCard(slot, true);
  }

  for (uint8_t i = 0; i < nPre && slot < RES_MAX; i++, slot++) {
    const Game& g = g_board[pre[i]];
    const StateInk& si = kStateInk[GS_PRE];
    setPlateCached(s_card[slot], &s_cPlate[slot], inkCode(GS_PRE, 0), si.plate);
    setTextCached(s_abbr[slot][0], s_cAbbr[slot][0], sizeof s_cAbbr[slot][0], g.away.abbr);
    setTextCached(s_abbr[slot][1], s_cAbbr[slot][1], sizeof s_cAbbr[slot][1], g.home.abbr);
    // No score placeholders: the start time already says it has not begun.
    setTextCached(s_score[slot][0], s_cScore[slot][0], sizeof s_cScore[slot][0], "");
    setTextCached(s_score[slot][1], s_cScore[slot][1], sizeof s_cScore[slot][1], "");
    for (int k = 0; k < 2; k++) {
      const uint8_t code = inkCode(GS_PRE, 1);
      if (s_cInk[slot][k] != code) {
        s_cInk[slot][k] = code;
        lv_obj_set_style_text_color(s_abbr[slot][k], si.ink, 0);
        // The score is empty here, but its colour rides the same cache — leave
        // the two in step or a slot that goes PRE then FINAL again skips it.
        lv_obj_set_style_text_color(s_score[slot][k], si.ink, 0);
      }
    }
    setTextCached(s_meta[slot], s_cMeta[slot], sizeof s_cMeta[slot], g.status);   // "7:00 PM"
    setInkCached(s_meta[slot], &s_cMetaInk[slot], inkCode(GS_PRE, 3), si.ink3);
    setTextCached(s_bcast[slot], s_cBcast[slot], sizeof s_cBcast[slot], g.bcast);
    setInkCached(s_bcast[slot], &s_cBcastInk[slot], inkCode(GS_PRE, 3), si.ink3);
    showCard(slot, true);
  }

  for (uint8_t i = slot; i < RES_MAX; i++) showCard(i, false);

  // The row re-centres on how many cards it actually filled. Gated on a
  // change: a layout pass per poll would rewrite every card's geometry for
  // nothing on the overwhelming majority of them.
  if (s_k != (int8_t)slot) {
    s_k = (int8_t)slot;
    uiLedgerLayoutK(uiRailOpen(), s_k);
  }

  if (!slot) {
    if (s_rootVis) { s_rootVis = false; lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN); }
    return;
  }
  if (!s_rootVis) { s_rootVis = true; lv_obj_clear_flag(s_root, LV_OBJ_FLAG_HIDDEN); }
}
