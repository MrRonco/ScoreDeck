// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
// ui_game.cpp — the tap-through detail. UI.md §4.
//
// The header reuses the tile's exact anatomy — same badges, same score sizes,
// same edge light — so the transition reads as the tile expanding rather than a
// new screen arriving. That continuity is free and it is what makes the tap
// feel physical.
#include "ui.h"
#include "theme.h"
#include "../config.h"
#include "../core/state.h"
#include "../net/api.h"

#define LS_COLS 14
#define PLAY_ROWS 5
#define STAT_ROWS 6

static lv_obj_t* s_root;
static lv_obj_t* s_edge;
static lv_obj_t* s_back;
static lv_obj_t* s_badgeA, *s_badgeH, *s_lblA, *s_lblH;
static lv_obj_t* s_abbrA, *s_abbrH, *s_recA, *s_recH, *s_scoreA, *s_scoreH;
static lv_obj_t* s_status, *s_venue;
static lv_obj_t* s_lsHdr[LS_COLS], *s_lsA[LS_COLS], *s_lsH[LS_COLS];
static lv_obj_t* s_lsTeamA, *s_lsTeamH;
static lv_obj_t* s_playT[PLAY_ROWS], *s_playX[PLAY_ROWS], *s_playS[PLAY_ROWS];
static lv_obj_t* s_playBar[PLAY_ROWS];
static lv_obj_t* s_statK[STAT_ROWS], *s_statA[STAT_ROWS], *s_statH[STAT_ROWS];
static lv_obj_t* s_wpA, *s_wpH, *s_wpBarA, *s_wpBarH, *s_wpWrap;
static lv_obj_t* s_loading;

static char s_openId[12];
static char s_openLeague[8];

lv_obj_t* uiGameRoot() { return s_root; }
const char* uiGameOpenId() { return s_openId; }

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

static void onBack(lv_event_t*) { uiFocusNoteUserClose(); uiGameClose(); }
static void onLineup(lv_event_t*) {
  if (s_openId[0]) uiLineupOpen(s_openLeague, s_openId);
}

void uiGameInit(lv_obj_t* parent) {
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

  // ── header: the tile, expanded ───────────────────────────────────────────
  lv_obj_t* hdr = glassPanel(s_root, 0, 0, SCR_W, 92, 0);
  s_edge = lv_obj_create(hdr);
  lv_obj_remove_style_all(s_edge);
  lv_obj_set_size(s_edge, 4, 90);
  lv_obj_set_pos(s_edge, 0, 0);
  lv_obj_set_style_bg_opa(s_edge, LV_OPA_COVER, 0);

  s_back = lv_btn_create(hdr);
  lv_obj_set_size(s_back, 54, 44);
  lv_obj_set_pos(s_back, 14, 24);
  lv_obj_set_style_bg_color(s_back, C_EDGE, 0);
  lv_obj_set_style_border_width(s_back, 0, 0);
  lv_obj_set_style_radius(s_back, 9, 0);
  lv_obj_add_event_cb(s_back, onBack, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* bl = lv_label_create(s_back);
  lv_label_set_text(bl, "<");
  lv_obj_set_style_text_font(bl, F_BODY, 0);   // F_ABBR has no glyph for "<"
  lv_obj_set_style_text_color(bl, C_INK, 0);
  lv_obj_center(bl);

  s_badgeA = teamBadge(hdr, "", 0x5D6D7E, 44);  lv_obj_set_pos(s_badgeA, 82, 24);
  s_lblA = lv_obj_get_child(s_badgeA, 0);
  s_abbrA  = lb(hdr, 136, 26, C_INK, F_ABBR);
  s_recA   = lb(hdr, 136, 50, C_INK3, F_MICRO);
  s_scoreA = lb(hdr, 210, 22, C_INK, F_SCORE);

  s_badgeH = teamBadge(hdr, "", 0x5D6D7E, 44);  lv_obj_set_pos(s_badgeH, SCR_W - 126, 24);
  s_lblH = lv_obj_get_child(s_badgeH, 0);
  s_abbrH  = lb(hdr, SCR_W - 214, 26, C_INK, F_ABBR, LV_TEXT_ALIGN_RIGHT, 74);
  s_recH   = lb(hdr, SCR_W - 214, 50, C_INK3, F_MICRO, LV_TEXT_ALIGN_RIGHT, 74);
  s_scoreH = lb(hdr, SCR_W - 300, 22, C_INK, F_SCORE, LV_TEXT_ALIGN_RIGHT, 76);

  // Status is upstream prose ("3rd 04:21", "Bot 7", "Final/OT") and carries
  // lowercase, so it cannot use the caps-only F_ABBR.
  s_status = lb(hdr, (SCR_W - 240) / 2, 24, C_INK, F_BODY, LV_TEXT_ALIGN_CENTER, 240);
  s_venue  = lb(hdr, (SCR_W - 240) / 2, 52, C_INK3, F_MICRO, LV_TEXT_ALIGN_CENTER, 240);

  // ── linescore ────────────────────────────────────────────────────────────
  lv_obj_t* ls = glassPanel(s_root, 16, 104, 768, 74, 12);
  s_lsTeamA = lb(ls, 16, 26, C_INK2, F_MICRO);
  s_lsTeamH = lb(ls, 16, 48, C_INK, F_MICRO);
  for (int i = 0; i < LS_COLS; i++) {
    const int x = 72 + i * 49;
    s_lsHdr[i] = lb(ls, x, 8,  C_INK3, F_MICRO, LV_TEXT_ALIGN_CENTER, 44);
    s_lsA[i]   = lb(ls, x, 26, C_INK2, F_MICRO, LV_TEXT_ALIGN_CENTER, 44);
    s_lsH[i]   = lb(ls, x, 48, C_INK,  F_MICRO, LV_TEXT_ALIGN_CENTER, 44);
  }

  // ── scoring plays ────────────────────────────────────────────────────────
  //
  // 520 wide, not 468. The two cards below split the row by their box sizes
  // rather than by what they carry: the scoring column ellipsised FIVE of five
  // plays — "Matthews (24) Power Play, from Marner and Nyland" losing half its
  // characters — while the stats card beside it centred a four-glyph key in a
  // 128 px box. The plays are prose that has to be read; the stats are three
  // short columns. The width follows the content.
  //
  // Sized from the type bound, per the rule at :132 below: statK is char[10],
  // so the widest key the type permits is 9 glyphs of F_MICRO's 7.8125 px
  // advance = 70.3 px. The stats card is therefore 14 + 60 + 8 + 72 + 8 + 60 +
  // 14 = 236, pinned to the 784 right frame, and the scoring card takes the
  // rest of the row less the 12 px gutter.
  lv_obj_t* pc = glassPanel(s_root, 16, 190, 520, 242, 12);
  lv_obj_t* ph = lb(pc, 16, 12, C_INK3, F_MICRO);
  lv_label_set_text(ph, "SCORING");
  for (int i = 0; i < PLAY_ROWS; i++) {
    const int y = 38 + i * 40;
    s_playBar[i] = lv_obj_create(pc);
    lv_obj_remove_style_all(s_playBar[i]);
    lv_obj_set_size(s_playBar[i], 3, 30);
    lv_obj_set_pos(s_playBar[i], 14, y);
    lv_obj_set_style_bg_opa(s_playBar[i], LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_playBar[i], 2, 0);
    // The clock column had no width and the play text began at x=92, so a full
    // "3rd 04:21" ran four pixels UNDER the first letter of the play.
    //
    // The width is derived, not guessed: F_MICRO is monospaced at an advance
    // of 125/16 = 7.8125 px, and GameDetail::playT is char[11], so the widest
    // string the type permits is 10 glyphs = 78.1 px. 82 clears it. A first
    // pass at 70 fitted the SAMPLE ("3rd 04:21", 9 glyphs = 70.3 px) and
    // ellipsised it by a third of a pixel — size from the bound, not the case
    // in front of you.
    s_playT[i] = lb(pc, 26, y, C_INK3, F_MICRO, LV_TEXT_ALIGN_LEFT, 82);
    lv_label_set_long_mode(s_playT[i], LV_LABEL_LONG_DOT);
    s_playX[i] = lb(pc, 114, y, C_INK2, F_BODY);   // play text names players
    // 326, up from 278 (+17.3%). The card's inner right edge is 520-16 = 504,
    // the score column takes the last 56, and 8 px separates them.
    lv_obj_set_width(s_playX[i], 326);
    // TWO lines, because one cannot be enough at any width this row can have.
    // GameDetail::playX is char[73], and a real play — "Auston Matthews scores
    // on a wrist shot from the slot, assisted by Nylander" — is 74 characters,
    // roughly 500 px of F_BODY. The row is 520 px wide in total and has to
    // carry a clock and a score as well, so widening alone could never do it:
    // before this change five of five plays ellipsised, one of them losing
    // half its text. Two lines at 326 px carry ~650 px of prose, which clears
    // the type bound. The 40 px row pitch already had the room (2 x 19 = 38).
    // LONG_DOT stays: if a future string overruns even two lines it must say
    // so rather than silently paint over the row below it.
    lv_obj_set_height(s_playX[i], 2 * (int)lv_font_get_line_height(F_BODY));
    lv_label_set_long_mode(s_playX[i], LV_LABEL_LONG_DOT);
    s_playS[i] = lb(pc, 448, y, C_INK, F_MICRO, LV_TEXT_ALIGN_RIGHT, 56);
  }

  // ── team comparison ──────────────────────────────────────────────────────
  lv_obj_t* sc = glassPanel(s_root, 548, 190, 236, 242, 12);
  lv_obj_t* sh = lb(sc, 16, 12, C_INK3, F_MICRO);
  lv_label_set_text(sh, "TEAM STATS");
  for (int i = 0; i < STAT_ROWS; i++) {
    const int y = 40 + i * 33;
    s_statA[i] = lb(sc, 14, y, C_INK2, F_MICRO, LV_TEXT_ALIGN_LEFT, 60);
    // 72, from the type bound (statK is char[10] = 9 glyphs = 70.3 px), not
    // from the fixture in front of us — "SOG"/"PP"/"FO%" are four glyphs and
    // sizing to those is how the 128 px box got a 31 px word in it. LONG_DOT
    // so a longer key ellipsises instead of running into the home value.
    s_statK[i] = lb(sc, 82, y, C_INK3, F_MICRO, LV_TEXT_ALIGN_CENTER, 72);
    lv_label_set_long_mode(s_statK[i], LV_LABEL_LONG_DOT);
    s_statH[i] = lb(sc, 162, y, C_INK, F_MICRO, LV_TEXT_ALIGN_RIGHT, 60);
  }

  // ── win probability ──────────────────────────────────────────────────────
  s_wpWrap = lv_obj_create(s_root);
  lv_obj_remove_style_all(s_wpWrap);
  lv_obj_set_size(s_wpWrap, 768, 22);
  lv_obj_set_pos(s_wpWrap, 16, 444);
  lv_obj_clear_flag(s_wpWrap, LV_OBJ_FLAG_SCROLLABLE);
  s_wpA = lb(s_wpWrap, 0, 0, C_INK3, F_MICRO);
  s_wpH = lb(s_wpWrap, 568, 0, C_INK2, F_MICRO, LV_TEXT_ALIGN_RIGHT, 200);
  s_wpBarA = lv_obj_create(s_wpWrap);
  lv_obj_remove_style_all(s_wpBarA);
  lv_obj_set_pos(s_wpBarA, 0, 16);
  lv_obj_set_size(s_wpBarA, 384, 5);
  // Was opa 150 against the home bar's COVER, so whoever led, the away half
  // looked weaker — a false signal in the one element whose whole job is a
  // like-for-like comparison.
  lv_obj_set_style_bg_opa(s_wpBarA, LV_OPA_COVER, 0);
  s_wpBarH = lv_obj_create(s_wpWrap);
  lv_obj_remove_style_all(s_wpBarH);
  lv_obj_set_pos(s_wpBarH, 384, 16);
  lv_obj_set_size(s_wpBarH, 384, 5);
  lv_obj_set_style_bg_opa(s_wpBarH, LV_OPA_COVER, 0);

  lv_obj_t* luBtn = lv_btn_create(s_root);
  lv_obj_set_size(luBtn, 96, 30);
  lv_obj_set_pos(luBtn, SCR_W - 16 - 96, 100);
  lv_obj_set_style_bg_color(luBtn, C_EDGE, 0);
  lv_obj_set_style_border_width(luBtn, 0, 0);
  lv_obj_set_style_radius(luBtn, 7, 0);
  lv_obj_add_event_cb(luBtn, onLineup, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* luLbl = lv_label_create(luBtn);
  lv_label_set_text(luLbl, "LINEUP");
  lv_obj_set_style_text_font(luLbl, F_MICRO, 0);
  lv_obj_set_style_text_color(luLbl, C_INK, 0);
  lv_obj_center(luLbl);

  s_loading = lb(s_root, 0, 250, C_INK3, F_BODY, LV_TEXT_ALIGN_CENTER, SCR_W);
  lv_label_set_text(s_loading, "Loading...");
}

/** Leading side gets its own colour lifted against the header's fill; the
 *  trailing side drops to ink-2. The header panel is the default glass, so the
 *  live surface is the right thing to solve against. */
static void setLeadInk(lv_obj_t* score, uint32_t colour, bool leading) {
  lv_obj_set_style_text_color(
      score,
      leading ? lv_color_hex(teamInkFor(colour, kStateInk[GS_LIVE].fill))
              : kStateInk[GS_LIVE].ink2,
      0);
}

void uiGameOpen(const Game& g) {
  strncpy(s_openId, g.id, sizeof s_openId - 1);
  s_openId[sizeof s_openId - 1] = '\0';
  strncpy(s_openLeague, g.league, sizeof s_openLeague - 1);
  s_openLeague[sizeof s_openLeague - 1] = '\0';

  // Paint what the board already knows immediately, so the tap is instant and
  // the fetch fills in the rest. Nothing worse than a blank screen on tap.
  lv_label_set_text(s_abbrA, g.away.abbr);   lv_label_set_text(s_lblA, g.away.abbr);
  lv_label_set_text(s_abbrH, g.home.abbr);   lv_label_set_text(s_lblH, g.home.abbr);
  lv_label_set_text(s_recA, g.away.rec);     lv_label_set_text(s_recH, g.home.rec);
  teamBadgeSet(s_badgeA, g.away.color);
  teamBadgeSet(s_badgeH, g.home.color);
  char b[8];
  snprintf(b, sizeof b, "%u", g.away.score); lv_label_set_text(s_scoreA, b);
  snprintf(b, sizeof b, "%u", g.home.score); lv_label_set_text(s_scoreH, b);
  setLeadInk(s_scoreA, g.away.color, g.state != GS_PRE && g.away.score > g.home.score);
  setLeadInk(s_scoreH, g.home.color, g.state != GS_PRE && g.home.score > g.away.score);
  lv_label_set_text(s_status, g.status);
  lv_label_set_text(s_venue, g.bcast);
  lv_obj_set_style_bg_color(s_edge,
      lv_color_hex(g.state == GS_LIVE ? (g.leaderHome ? g.home.color : g.away.color) : 0x2A3646), 0);
  lv_obj_clear_flag(s_loading, LV_OBJ_FLAG_HIDDEN);

  uiShow(SCR_GAME);
  apiGameStart(g.league, g.id);
}

void uiGameClose() {
  s_openId[0] = '\0';
  uiShow(uiShouldIdle() ? SCR_IDLE : SCR_BOARD);
}

bool uiGameIsOpen() { return s_openId[0] != '\0'; }

void uiGameApply(const GameDetail& d) {
  if (!uiGameIsOpen() || strcmp(d.id, s_openId) != 0) return;   // stale response
  lv_obj_add_flag(s_loading, LV_OBJ_FLAG_HIDDEN);

  lv_label_set_text(s_status, d.status);
  if (d.venue[0]) lv_label_set_text(s_venue, d.venue);
  char b[8];
  snprintf(b, sizeof b, "%u", d.awayScore); lv_label_set_text(s_scoreA, b);
  snprintf(b, sizeof b, "%u", d.homeScore); lv_label_set_text(s_scoreH, b);
  // Leading score in the leading team's own colour, same as the board's tiles
  // and hero. The header reuses the tile's anatomy, so it reuses its rules —
  // the alternative is a game screen where the winner is grey and the tile you
  // tapped to reach it was not.
  setLeadInk(s_scoreA, d.awayColor, d.awayScore > d.homeScore);
  setLeadInk(s_scoreH, d.homeColor, d.homeScore > d.awayScore);

  lv_label_set_text(s_lsTeamA, d.awayAbbr);
  lv_label_set_text(s_lsTeamH, d.homeAbbr);
  for (int i = 0; i < LS_COLS; i++) {
    const bool on = i < d.lsCount;
    lv_label_set_text(s_lsHdr[i], on ? d.lsCols[i] : "");
    lv_label_set_text(s_lsA[i],   on ? d.lsA[i] : "");
    lv_label_set_text(s_lsH[i],   on ? d.lsH[i] : "");
  }

  for (int i = 0; i < PLAY_ROWS; i++) {
    const bool on = i < d.playCount;
    lv_label_set_text(s_playT[i], on ? d.playT[i] : "");
    lv_label_set_text(s_playX[i], on ? d.playX[i] : "");
    lv_label_set_text(s_playS[i], on ? d.playS[i] : "");
    if (on) {
      lv_obj_clear_flag(s_playBar[i], LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_style_bg_color(s_playBar[i],
          lv_color_hex(d.playHome[i] ? d.homeColor : d.awayColor), 0);
    } else {
      lv_obj_add_flag(s_playBar[i], LV_OBJ_FLAG_HIDDEN);
    }
  }

  for (int i = 0; i < STAT_ROWS; i++) {
    const bool on = i < d.statCount;
    lv_label_set_text(s_statK[i], on ? d.statK[i] : "");
    lv_label_set_text(s_statA[i], on ? d.statA[i] : "");
    lv_label_set_text(s_statH[i], on ? d.statH[i] : "");
  }

  if (d.winProbHome <= 100) {
    lv_obj_clear_flag(s_wpWrap, LV_OBJ_FLAG_HIDDEN);
    char t[40];
    snprintf(t, sizeof t, "%s %u%%", d.awayAbbr, 100 - d.winProbHome);
    lv_label_set_text(s_wpA, t);
    snprintf(t, sizeof t, "%s %u%%", d.homeAbbr, d.winProbHome);
    lv_label_set_text(s_wpH, t);
    const int hw = 768 * d.winProbHome / 100;
    lv_obj_set_size(s_wpBarA, 768 - hw, 5);
    lv_obj_set_pos(s_wpBarA, 0, 16);
    lv_obj_set_size(s_wpBarH, hw, 5);
    lv_obj_set_pos(s_wpBarH, 768 - hw, 16);
    lv_obj_set_style_bg_color(s_wpBarA, lv_color_hex(d.awayColor), 0);
    lv_obj_set_style_bg_color(s_wpBarH, lv_color_hex(d.homeColor), 0);
  } else {
    lv_obj_add_flag(s_wpWrap, LV_OBJ_FLAG_HIDDEN);
  }
}
