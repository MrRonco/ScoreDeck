// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
// ui_lineup.cpp — lineup list and the player sheet. UI.md §5.
//
// ESPN does NOT return hockey line combinations — the grouping it gives is
// forwards / defense / goalies, not L1/L2/L3. Showing invented line pairings
// would be a lie, so this renders the real grouping with tonight's numbers.
//
// The player sheet is a FIXED rect that fades in and never slides: a moving
// frosted panel would need live blur, which UI.md §1 rules out.
#include "ui.h"
#include "theme.h"
#include "../config.h"
#include "../core/state.h"
#include "../net/api.h"
#include "../net/logos.h"

#define ROWS 11

static lv_obj_t* s_root;
static lv_obj_t* s_title;
static lv_obj_t* s_hint;
static lv_obj_t* s_sideBtn[2];
static lv_obj_t* s_sideLbl[2];
static lv_obj_t* s_colHdr[LU_COLS];
static lv_obj_t* s_rowObj[ROWS];
static lv_obj_t* s_rowPos[ROWS];
static lv_obj_t* s_rowName[ROWS];
static lv_obj_t* s_rowVal[ROWS][LU_COLS];
static lv_obj_t* s_rowGroup[ROWS];

// player sheet
static lv_obj_t* s_sheet;
static lv_obj_t* s_shEdge, *s_shBadge, *s_shBadgeLbl, *s_shName, *s_shMeta1, *s_shMeta2;
static lv_obj_t* s_shStatK[PC_STATS], *s_shStatV[PC_STATS], *s_shStatR[PC_STATS], *s_shBar[PC_STATS];
static lv_obj_t* s_shLoading;
static lv_obj_t* s_shPhoto;
static char s_shAthlete[12];
static char s_shLeague[8];

static uint8_t s_side;
static uint8_t s_scroll;
static char    s_league[8];
static char    s_gameId[12];

/** Flattened view: group headers interleaved with players. */
struct Line { bool header; uint8_t g; uint8_t p; };
static Line   s_lines[LU_GROUPS * (LU_PLAYERS + 1)];
static uint8_t s_lineCount;

lv_obj_t* uiLineupRoot() { return s_root; }

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

static int colX(uint8_t i, uint8_t n) { return 768 - 18 - (n - i) * 76; }

static void rebuildLines() {
  s_lineCount = 0;
  if (s_side >= g_lineup.sideCount) return;
  const LineSide& S = g_lineup.sides[s_side];
  for (uint8_t g = 0; g < S.groupCount; g++) {
    s_lines[s_lineCount++] = { true, g, 0 };
    for (uint8_t p = 0; p < S.groups[g].count && s_lineCount < (uint8_t)(LU_GROUPS * (LU_PLAYERS + 1)); p++)
      s_lines[s_lineCount++] = { false, g, p };
  }
}

static void onBack(lv_event_t*) {
  if (!lv_obj_has_flag(s_sheet, LV_OBJ_FLAG_HIDDEN)) { uiPlayerClose(); return; }
  uiLineupClose();
}
static void onSide(lv_event_t* e) {
  s_side = (uint8_t)(intptr_t)lv_event_get_user_data(e);
  s_scroll = 0;
  rebuildLines();
  uiLineupRender();
}
static void onRow(lv_event_t* e) {
  const uint8_t r = (uint8_t)(intptr_t)lv_event_get_user_data(e);
  const uint8_t idx = s_scroll + r;
  if (idx >= s_lineCount || s_lines[idx].header) return;
  const LineSide& S = g_lineup.sides[s_side];
  const LinePlayer& P = S.groups[s_lines[idx].g].players[s_lines[idx].p];
  uiPlayerOpen(s_league, P.id);
}
static void onGesture(lv_event_t*) {
  const lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_get_act());
  if (d == LV_DIR_TOP && s_scroll + ROWS < s_lineCount) { s_scroll++; uiLineupRender(); }
  else if (d == LV_DIR_BOTTOM && s_scroll) { s_scroll--; uiLineupRender(); }
}
static void onSheetDismiss(lv_event_t*) { uiPlayerClose(); }

void uiLineupInit(lv_obj_t* parent) {
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
  lv_obj_set_size(back, 48, 34); lv_obj_set_pos(back, 14, 7);
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
  s_hint  = lb(bar, SCR_W - 18 - 240, 17, C_INK3, F_MICRO, LV_TEXT_ALIGN_RIGHT, 240);

  for (uint8_t i = 0; i < 2; i++) {
    s_sideBtn[i] = lv_btn_create(bar);
    lv_obj_set_size(s_sideBtn[i], 74, 30);
    lv_obj_set_pos(s_sideBtn[i], 200 + i * 82, 9);
    lv_obj_set_style_border_width(s_sideBtn[i], 0, 0);
    lv_obj_set_style_radius(s_sideBtn[i], 7, 0);
    lv_obj_add_event_cb(s_sideBtn[i], onSide, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    s_sideLbl[i] = lv_label_create(s_sideBtn[i]);
    lv_obj_set_style_text_font(s_sideLbl[i], F_MICRO, 0);
    lv_obj_center(s_sideLbl[i]);
  }

  lv_obj_t* card = glassPanel(s_root, 16, 60, 768, 404, 12);
  for (uint8_t c = 0; c < LU_COLS; c++)
    s_colHdr[c] = lb(card, 0, 8, C_INK3, F_MICRO, LV_TEXT_ALIGN_RIGHT, 70);

  for (uint8_t r = 0; r < ROWS; r++) {
    const int y = 28 + r * 34;
    s_rowObj[r] = lv_obj_create(card);
    lv_obj_remove_style_all(s_rowObj[r]);
    // Full card width at x=0: the row shares the card's coordinate space, so
     // colX() means the same thing for a value as it does for its header.
    lv_obj_set_size(s_rowObj[r], 764, 32);
    lv_obj_set_pos(s_rowObj[r], 0, y);
    lv_obj_set_style_bg_opa(s_rowObj[r], LV_OPA_TRANSP, 0);
    lv_obj_add_flag(s_rowObj[r], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_rowObj[r], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_rowObj[r], onRow, LV_EVENT_SHORT_CLICKED, (void*)(intptr_t)r);

    s_rowGroup[r] = lb(s_rowObj[r], 16, 9, C_INK3, F_MICRO);
    s_rowPos[r]   = lb(s_rowObj[r], 16, 8, C_INK3, F_NUM, LV_TEXT_ALIGN_LEFT, 34);
    s_rowName[r]  = lb(s_rowObj[r], 56, 5, C_INK, F_BODY);   // names carry accents
    for (uint8_t c = 0; c < LU_COLS; c++)
      s_rowVal[r][c] = lb(s_rowObj[r], 0, 7, C_INK2, F_NUM, LV_TEXT_ALIGN_RIGHT, 70);
  }

  // ── player sheet: fixed rect, fades in, never slides ─────────────────────
  s_sheet = lv_obj_create(s_root);
  lv_obj_remove_style_all(s_sheet);
  lv_obj_set_size(s_sheet, SCR_W, SCR_H);
  lv_obj_set_pos(s_sheet, 0, 0);
  lv_obj_set_style_bg_color(s_sheet, lv_color_hex(0x04070C), 0);
  lv_obj_set_style_bg_opa(s_sheet, 150, 0);
  lv_obj_add_flag(s_sheet, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(s_sheet, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_sheet, onSheetDismiss, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_flag(s_sheet, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* sc = glassPanel(s_sheet, 380, 140, 404, 324, 14);
  s_shEdge = lv_obj_create(sc);
  lv_obj_remove_style_all(s_shEdge);
  lv_obj_set_size(s_shEdge, 3, 322);
  lv_obj_set_pos(s_shEdge, 0, 0);
  lv_obj_set_style_bg_opa(s_shEdge, LV_OPA_COVER, 0);

  s_shBadge = teamBadge(sc, "", 0x5D6D7E, 68);
  lv_obj_set_pos(s_shBadge, 18, 16);
  lv_obj_set_style_radius(s_shBadge, 12, 0);
  s_shBadgeLbl = lv_obj_get_child(s_shBadge, 0);

  // Headshot sits exactly on the badge and replaces it when one exists. 68 px
  // native, so no zoom and no clipping to get wrong.
  s_shPhoto = lv_img_create(sc);
  lv_img_set_antialias(s_shPhoto, true);
  lv_obj_set_pos(s_shPhoto, 18, 16);
  lv_obj_add_flag(s_shPhoto, LV_OBJ_FLAG_HIDDEN);

  s_shName  = lb(sc, 100, 20, C_INK, F_BODY);   // Archivo caps face has no accents
  lv_obj_set_width(s_shName, 286);
  lv_label_set_long_mode(s_shName, LV_LABEL_LONG_DOT);
  s_shMeta1 = lb(sc, 100, 44, C_INK3, F_MICRO);
  s_shMeta2 = lb(sc, 100, 62, C_INK3, F_MICRO);

  lv_obj_t* sh = lb(sc, 18, 104, C_INK3, F_MICRO);
  lv_label_set_text(sh, "SEASON        RANK IN LEAGUE");
  for (uint8_t i = 0; i < PC_STATS; i++) {
    const int y = 128 + i * 36;
    s_shStatK[i] = lb(sc, 18, y + 4, C_INK3, F_MICRO, LV_TEXT_ALIGN_LEFT, 56);
    s_shStatV[i] = lb(sc, 78, y, C_INK, F_ABBR, LV_TEXT_ALIGN_RIGHT, 54);
    s_shBar[i] = lv_obj_create(sc);
    lv_obj_remove_style_all(s_shBar[i]);
    lv_obj_set_size(s_shBar[i], 130, 3);
    lv_obj_set_pos(s_shBar[i], 142, y + 11);
    lv_obj_set_style_radius(s_shBar[i], 2, 0);
    lv_obj_set_style_bg_opa(s_shBar[i], LV_OPA_COVER, 0);
    lv_obj_add_flag(s_shBar[i], LV_OBJ_FLAG_HIDDEN);
    s_shStatR[i] = lb(sc, 282, y + 4, C_INK2, F_MICRO, LV_TEXT_ALIGN_RIGHT, 104);
  }
  s_shLoading = lb(sc, 0, 150, C_INK3, F_MICRO, LV_TEXT_ALIGN_CENTER, 404);
  lv_label_set_text(s_shLoading, "Loading...");
}

void uiLineupRender() {
  const Lineup& L = g_lineup;
  char buf[40];
  snprintf(buf, sizeof buf, "LINEUP");
  lv_label_set_text(s_title, buf);
  if (!L.sideCount) {
    lv_label_set_text(s_hint, L.loading ? "loading" : "no lineup for this game");
  } else {
    const LineSide& S = L.sides[s_side];
    if (S.formation[0]) snprintf(buf, sizeof buf, "%s  %s", S.abbr, S.formation);
    else {
      // s_lineCount is the FLATTENED line count, which includes the group
      // headings — a three-group hockey box score over-counted by three.
      uint8_t people = 0;
      for (uint8_t i = 0; i < s_lineCount; i++) if (!s_lines[i].header) people++;
      snprintf(buf, sizeof buf, "%u players", people);
    }
    lv_label_set_text(s_hint, buf);
  }

  for (uint8_t i = 0; i < 2; i++) {
    const bool on = i < L.sideCount;
    if (!on) { lv_obj_add_flag(s_sideBtn[i], LV_OBJ_FLAG_HIDDEN); continue; }
    lv_obj_clear_flag(s_sideBtn[i], LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_sideLbl[i], L.sides[i].abbr);
    const bool sel = (i == s_side);
    lv_obj_set_style_bg_color(s_sideBtn[i], sel ? lv_color_hex(L.sides[i].color) : C_EDGE, 0);
    lv_obj_set_style_text_color(s_sideLbl[i], sel ? lv_color_white() : C_INK3, 0);
  }

  const LineSide* S = (s_side < L.sideCount) ? &L.sides[s_side] : nullptr;
  const uint8_t nCols = (S && S->groupCount) ? S->groups[0].colCount : 0;
  for (uint8_t c = 0; c < LU_COLS; c++) {
    const bool on = c < nCols;
    lv_obj_set_pos(s_colHdr[c], colX(c, nCols), 8);
    lv_label_set_text(s_colHdr[c], on ? S->groups[0].cols[c] : "");
  }

  for (uint8_t r = 0; r < ROWS; r++) {
    const uint8_t idx = s_scroll + r;
    if (!S || idx >= s_lineCount) { lv_obj_add_flag(s_rowObj[r], LV_OBJ_FLAG_HIDDEN); continue; }
    lv_obj_clear_flag(s_rowObj[r], LV_OBJ_FLAG_HIDDEN);
    const Line& ln = s_lines[idx];
    const LineGroup& G = S->groups[ln.g];

    if (ln.header) {
      lv_label_set_text(s_rowGroup[r], G.name);
      lv_label_set_text(s_rowPos[r], "");
      lv_label_set_text(s_rowName[r], "");
      for (uint8_t c = 0; c < LU_COLS; c++) lv_label_set_text(s_rowVal[r][c], "");
      lv_obj_set_style_bg_opa(s_rowObj[r], LV_OPA_TRANSP, 0);
      continue;
    }
    const LinePlayer& P = G.players[ln.p];
    lv_label_set_text(s_rowGroup[r], "");
    lv_label_set_text(s_rowPos[r], P.jersey[0] ? P.jersey : P.pos);
    lv_label_set_text(s_rowName[r], P.name);
    lv_obj_set_style_text_color(s_rowName[r], P.starter || !S->formation[0] ? C_INK : C_INK3, 0);
    for (uint8_t c = 0; c < LU_COLS; c++) {
      const bool on = c < G.colCount;
      lv_obj_set_pos(s_rowVal[r][c], colX(c, G.colCount), 8);
      lv_label_set_text(s_rowVal[r][c], on ? P.vals[c] : "");
    }
  }
}

void uiLineupOpen(const char* league, const char* gameId) {
  strncpy(s_league, league, sizeof s_league - 1); s_league[sizeof s_league - 1] = '\0';
  strncpy(s_gameId, gameId, sizeof s_gameId - 1); s_gameId[sizeof s_gameId - 1] = '\0';
  s_side = 0; s_scroll = 0; s_lineCount = 0;
  g_lineup.loading = true;
  g_lineup.sideCount = 0;
  uiShow(SCR_LINEUP);
  uiLineupRender();
  apiLineupStart(s_league, s_gameId);
}

void uiLineupApply() {
  rebuildLines();
  uiLineupRender();
}

void uiLineupClose() { uiShow(uiShouldIdle() ? SCR_IDLE : SCR_BOARD); }
bool uiLineupIsOpen() { return uiCurrent() == SCR_LINEUP; }

// ── player sheet ───────────────────────────────────────────────────────────

void uiPlayerOpen(const char* league, const char* athleteId) {
  strncpy(s_shAthlete, athleteId, sizeof s_shAthlete - 1); s_shAthlete[sizeof s_shAthlete - 1] = 0;
  strncpy(s_shLeague, league, sizeof s_shLeague - 1); s_shLeague[sizeof s_shLeague - 1] = 0;
  lv_obj_add_flag(s_shPhoto, LV_OBJ_FLAG_HIDDEN);
  g_player.loading = true;
  g_player.name[0] = '\0';
  lv_obj_clear_flag(s_sheet, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(s_sheet);
  lv_obj_clear_flag(s_shLoading, LV_OBJ_FLAG_HIDDEN);
  uiPlayerRender();
  apiPlayerStart(league, athleteId);
}

void uiPlayerClose() { lv_obj_add_flag(s_sheet, LV_OBJ_FLAG_HIDDEN); }
bool uiPlayerIsOpen() { return s_sheet && !lv_obj_has_flag(s_sheet, LV_OBJ_FLAG_HIDDEN); }

void uiPlayerRender() {
  const PlayerCard& P = g_player;
  if (!P.name[0]) return;
  lv_obj_add_flag(s_shLoading, LV_OBJ_FLAG_HIDDEN);

  lv_obj_set_style_bg_color(s_shEdge, lv_color_hex(P.color), 0);
  lv_obj_set_style_bg_color(s_shBadge, lv_color_hex(P.color), 0);
  lv_label_set_text(s_shBadgeLbl, P.jersey[0] ? P.jersey : P.team);

  // Photo when one is built, jersey badge otherwise. The badge IS the fallback,
  // not a placeholder — an install that never runs the headshot build still
  // looks deliberate. docs/OPEN_SOURCE.md §1.
  const lv_img_dsc_t* shot = headshotGet(s_shAthlete);
  if (shot) {
    lv_img_set_src(s_shPhoto, shot);
    lv_obj_clear_flag(s_shPhoto, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_shBadge, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(s_shPhoto, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_shBadge, LV_OBJ_FLAG_HIDDEN);
    if (P.hasImage) headshotRequest(s_shLeague, s_shAthlete);
  }
  lv_label_set_text(s_shName, P.name);

  char m[64];
  snprintf(m, sizeof m, "%s%s%s", P.pos, P.team[0] ? "  -  " : "", P.team);
  lv_label_set_text(s_shMeta1, m);
  if (P.height[0] || P.weight[0] || P.age) {
    snprintf(m, sizeof m, "%s  %s%s%u", P.height, P.weight, P.age ? "  AGE " : "", P.age);
    lv_label_set_text(s_shMeta2, m);
  } else {
    lv_label_set_text(s_shMeta2, "");
  }

  for (uint8_t i = 0; i < PC_STATS; i++) {
    const bool on = i < P.statCount;
    lv_label_set_text(s_shStatK[i], on ? P.statK[i] : "");
    lv_label_set_text(s_shStatV[i], on ? P.statV[i] : "");
    lv_label_set_text(s_shStatR[i], on ? P.statR[i] : "");
    if (on && P.statR[i][0]) {
      // Rank text like "Tied-61st" or "124th" — bar length reads the leading
      // number so a top-10 finish looks different from a 300th.
      int rank = atoi(strpbrk(P.statR[i], "0123456789") ? strpbrk(P.statR[i], "0123456789") : "0");
      if (rank <= 0) rank = 400;
      const int w = rank <= 10 ? 130 : rank >= 300 ? 12 : 130 - (rank * 118) / 300;
      lv_obj_set_size(s_shBar[i], w < 6 ? 6 : w, 3);
      lv_obj_set_style_bg_color(s_shBar[i], lv_color_hex(rank <= 25 ? P.color : 0x3A4757), 0);
      lv_obj_clear_flag(s_shBar[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(s_shBar[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
}
