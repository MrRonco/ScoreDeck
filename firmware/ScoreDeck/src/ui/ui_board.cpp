// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
// ui_board.cpp — the 9-up board. Tiles are built ONCE and updated in place.
//
// Every dynamic write is change-cached. LVGL repaints an object even when a
// write does not change it, and unchanged writes fight the panel DMA — that was
// AirRadar's "wiggle". INHERITED_RULES.md §8.
#include "ui.h"
#include "theme.h"
#include "../config.h"
#include "../core/state.h"
#include <time.h>

static Screen s_screen = SCR_BOARD;

static lv_obj_t* s_board;      // page root
static lv_obj_t* s_bar;
static lv_obj_t* s_lblClock;
static lv_obj_t* s_lblDate;
static lv_obj_t* s_lblStatus;
static lv_obj_t* s_lblPage;

struct TileUI {
  lv_obj_t* root;
  lv_obj_t* edge;
  lv_obj_t* badge[2];
  lv_obj_t* badgeLbl[2];
  lv_obj_t* abbr[2];
  lv_obj_t* rec[2];
  lv_obj_t* score[2];
  lv_obj_t* status;
  lv_obj_t* bcast;
  // change cache — the whole point of this struct
  char     cAbbr[2][5];
  char     cRec[2][10];
  int32_t  cScore[2];
  uint32_t cColor[2];
  char     cStatus[16];
  char     cBcast[13];
  uint32_t cEdge;
  bool     cEdgeVis;
  lv_opa_t cOpa;
  bool     cUsed;
};
static TileUI s_tile[TILES_PER_PAGE];

static const DensitySpec& spec() { return kDensity[g_set.density]; }

// ── helpers ────────────────────────────────────────────────────────────────
static void setTextCached(lv_obj_t* o, char* cache, size_t cap, const char* v) {
  if (strncmp(cache, v, cap - 1) == 0) return;
  strncpy(cache, v, cap - 1);
  cache[cap - 1] = '\0';
  lv_label_set_text(o, cache);
}
static void setNumCached(lv_obj_t* o, int32_t* cache, int32_t v) {
  if (*cache == v) return;
  *cache = v;
  char b[8];
  snprintf(b, sizeof b, "%ld", (long)v);
  lv_label_set_text(o, b);
}
static void setHiddenCached(lv_obj_t* o, bool* cache, bool hide) {
  if (*cache == !hide) return;   // cache stores "visible"
  *cache = !hide;
  if (hide) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  else      lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
}

// ── build ──────────────────────────────────────────────────────────────────
static lv_obj_t* microLabel(lv_obj_t* p, int x, int y, lv_color_t col, const lv_font_t* f) {
  lv_obj_t* l = lv_label_create(p);
  lv_obj_set_pos(l, x, y);
  lv_obj_set_style_text_color(l, col, 0);
  lv_obj_set_style_text_font(l, f, 0);
  lv_label_set_text(l, "");
  return l;
}

static void buildTile(TileUI& t, int idx) {
  const DensitySpec& d = spec();
  const int col = idx % d.cols, row = idx / d.cols;
  const int x = d.marg + col * (d.tileW + d.gut);
  const int y = d.gridTop + row * (d.tileH + d.gut);

  t.root = glassPanel(s_board, x, y, d.tileW, d.tileH, 12);

  // The signature: a 3px luminous strip on the LEADING team's side, which
  // swaps ends when the lead changes. UI.md §2.
  t.edge = lv_obj_create(t.root);
  lv_obj_remove_style_all(t.edge);
  lv_obj_set_size(t.edge, EDGE_W, d.tileH - 2);
  lv_obj_set_pos(t.edge, 0, 0);
  lv_obj_set_style_bg_opa(t.edge, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(t.edge, 2, 0);
  lv_obj_add_flag(t.edge, LV_OBJ_FLAG_HIDDEN);

  const int rowH = (d.tileH - 2 * TILE_PAD_Y - STATUS_H) / 2;
  for (int i = 0; i < 2; i++) {
    const int ry = TILE_PAD_Y + i * rowH;
    t.badge[i] = lv_obj_create(t.root);
    lv_obj_remove_style_all(t.badge[i]);
    lv_obj_set_size(t.badge[i], d.badge, d.badge);
    lv_obj_set_pos(t.badge[i], TILE_PAD_X, ry + (rowH - d.badge) / 2);
    lv_obj_set_style_radius(t.badge[i], 7, 0);
    lv_obj_set_style_bg_opa(t.badge[i], LV_OPA_COVER, 0);
    lv_obj_clear_flag(t.badge[i], LV_OBJ_FLAG_SCROLLABLE);
    t.badgeLbl[i] = lv_label_create(t.badge[i]);
    lv_obj_set_style_text_font(t.badgeLbl[i], F_MICRO, 0);
    lv_obj_set_style_text_color(t.badgeLbl[i], lv_color_white(), 0);
    lv_label_set_text(t.badgeLbl[i], "");
    lv_obj_center(t.badgeLbl[i]);

    const int tx = TILE_PAD_X + d.badge + 10;
    t.abbr[i]  = microLabel(t.root, tx, ry + 2, C_INK, F_ABBR);
    t.rec[i]   = microLabel(t.root, tx, ry + 21, C_INK3, F_MICRO);

    t.score[i] = lv_label_create(t.root);
    lv_obj_set_style_text_font(t.score[i], F_SCORE, 0);
    lv_obj_set_style_text_color(t.score[i], C_INK, 0);
    lv_obj_set_style_text_align(t.score[i], LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(t.score[i], 74);
    lv_obj_set_pos(t.score[i], d.tileW - TILE_PAD_X - 74, ry + (rowH - 28) / 2);
    lv_label_set_text(t.score[i], "");
  }

  t.status = microLabel(t.root, TILE_PAD_X, d.tileH - TILE_PAD_Y - 13, C_INK2, F_MICRO);
  t.bcast  = lv_label_create(t.root);
  lv_obj_set_style_text_font(t.bcast, F_MICRO, 0);
  lv_obj_set_style_text_color(t.bcast, C_INK3, 0);
  lv_obj_set_style_text_align(t.bcast, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_width(t.bcast, 86);
  lv_obj_set_pos(t.bcast, d.tileW - TILE_PAD_X - 86, d.tileH - TILE_PAD_Y - 13);
  lv_label_set_text(t.bcast, "");

  memset(t.cAbbr, 0, sizeof t.cAbbr);
  memset(t.cRec, 0, sizeof t.cRec);
  memset(t.cStatus, 0, sizeof t.cStatus);
  memset(t.cBcast, 0, sizeof t.cBcast);
  t.cScore[0] = t.cScore[1] = -1;
  t.cColor[0] = t.cColor[1] = 0xFFFFFFFF;
  t.cEdge = 0xFFFFFFFF;
  t.cEdgeVis = true;
  t.cOpa = 0;
  t.cUsed = true;
}

static void buildBar() {
  s_bar = glassPanel(s_board, 0, 0, SCR_W, spec().barH, 0);
  s_lblClock = microLabel(s_bar, 18, 12, C_INK, F_ABBR);
  s_lblDate  = microLabel(s_bar, 84, 17, C_INK2, F_MICRO);
  s_lblStatus = lv_label_create(s_bar);
  lv_obj_set_style_text_font(s_lblStatus, F_MICRO, 0);
  lv_obj_set_style_text_color(s_lblStatus, C_INK2, 0);
  lv_obj_set_style_text_align(s_lblStatus, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_width(s_lblStatus, 300);
  lv_obj_set_pos(s_lblStatus, SCR_W - 18 - 300, 17);
  lv_label_set_text(s_lblStatus, "starting");

  s_lblPage = lv_label_create(s_board);
  lv_obj_set_style_text_font(s_lblPage, F_MICRO, 0);
  lv_obj_set_style_text_color(s_lblPage, C_INK3, 0);
  lv_obj_set_style_text_align(s_lblPage, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(s_lblPage, 200);
  lv_obj_set_pos(s_lblPage, (SCR_W - 200) / 2, SCR_H - 14);
  lv_label_set_text(s_lblPage, "");
}

// ── paging / filter ────────────────────────────────────────────────────────
static bool passesFilter(const Game& g) {
  if (g_leagueFilter < 0) return true;
  if (g_leagueFilter >= g_leagueCount) return true;
  return strcmp(g.league, g_leagues[g_leagueFilter].slug) == 0;
}

static uint8_t visibleCount() {
  uint8_t n = 0;
  for (uint8_t i = 0; i < g_gameCount; i++) if (passesFilter(g_board[i])) n++;
  return n;
}

static void onTilePressed(lv_event_t* e) {
  // Phase 3 target: open game detail. For now cycle density on long press so
  // the setting is reachable and testable without a settings screen.
  if (lv_event_get_code(e) == LV_EVENT_LONG_PRESSED) {
    g_set.density = (g_set.density + 1) % 3;
    settingsSave();
    uiInit();            // rebuild at the new geometry
    uiBoardRefresh();
  }
}

static void onBoardPressed(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_SHORT_CLICKED) return;
  const uint8_t per = spec().cols * spec().rows;
  const uint8_t pages = (visibleCount() + per - 1) / per;
  if (pages > 1) { g_page = (g_page + 1) % pages; uiBoardRefresh(); }
}

// ── refresh ────────────────────────────────────────────────────────────────
void uiBoardRefresh() {
  const DensitySpec& d = spec();
  const uint8_t per = d.cols * d.rows;
  const uint8_t pages = max<uint8_t>(1, (visibleCount() + per - 1) / per);
  if (g_page >= pages) g_page = 0;

  uint8_t seen = 0, slot = 0;
  for (uint8_t i = 0; i < g_gameCount && slot < per; i++) {
    const Game& g = g_board[i];
    if (!passesFilter(g)) continue;
    if (seen++ < g_page * per) continue;

    TileUI& t = s_tile[slot];
    setHiddenCached(t.root, &t.cUsed, false);

    // Luminance encodes state — live 100%, scheduled 72%, final 55%.
    const lv_opa_t opa = g.state == GS_LIVE ? OPA_LIVE : g.state == GS_PRE ? OPA_PRE : OPA_FINAL;
    if (t.cOpa != opa) { t.cOpa = opa; lv_obj_set_style_opa(t.root, opa, 0); }

    const Side* side[2] = { &g.away, &g.home };
    for (int k = 0; k < 2; k++) {
      setTextCached(t.abbr[k], t.cAbbr[k], sizeof t.cAbbr[k], side[k]->abbr);
      setTextCached(t.rec[k],  t.cRec[k],  sizeof t.cRec[k],  side[k]->rec);
      if (g.state == GS_PRE) {
        if (t.cScore[k] != -2) { t.cScore[k] = -2; lv_label_set_text(t.score[k], "-"); }
      } else {
        setNumCached(t.score[k], &t.cScore[k], side[k]->score);
      }
      // The losing side drops to ink-2 — the second half of the state channel.
      const bool leading = (g.state != GS_PRE) &&
                           ((k == 1) == g.leaderHome) && (g.away.score != g.home.score);
      lv_obj_set_style_text_color(t.score[k], leading ? C_INK : C_INK2, 0);

      if (t.cColor[k] != side[k]->color) {
        t.cColor[k] = side[k]->color;
        lv_obj_set_style_bg_color(t.badge[k], lv_color_hex(side[k]->color), 0);
      }
      lv_label_set_text(t.badgeLbl[k], side[k]->abbr);
    }

    setTextCached(t.status, t.cStatus, sizeof t.cStatus, g.status);
    setTextCached(t.bcast,  t.cBcast,  sizeof t.cBcast,  g.bcast);

    // Edge light: only live games, on the leading side.
    const bool edgeOn = (g.state == GS_LIVE);
    setHiddenCached(t.edge, &t.cEdgeVis, !edgeOn);
    if (edgeOn) {
      const uint32_t c = g.leaderHome ? g.home.color : g.away.color;
      if (t.cEdge != c) { t.cEdge = c; lv_obj_set_style_bg_color(t.edge, lv_color_hex(c), 0); }
      lv_obj_set_x(t.edge, g.leaderHome ? d.tileW - EDGE_W - 1 : 0);
    }
    slot++;
  }

  for (; slot < per; slot++) setHiddenCached(s_tile[slot].root, &s_tile[slot].cUsed, true);

  char pg[24] = "";
  if (pages > 1) snprintf(pg, sizeof pg, "%u / %u", g_page + 1, pages);
  lv_label_set_text(s_lblPage, pg);
  uiSetStatus();
}

void uiSetClock(const char* hhmm, const char* date) {
  lv_label_set_text(s_lblClock, hhmm);
  lv_label_set_text(s_lblDate, date);
}

void uiSetStatus() {
  static char last[96] = "";
  uint8_t live = 0;
  for (uint8_t i = 0; i < g_gameCount; i++) if (g_board[i].state == GS_LIVE) live++;

  char buf[96];
  switch (g_net) {
    case NET_NOWIFI:  snprintf(buf, sizeof buf, "no wi-fi"); break;
    case NET_NOPROXY: snprintf(buf, sizeof buf, "no proxy configured"); break;
    case NET_ERR:     snprintf(buf, sizeof buf, "%s", g_netDetail[0] ? g_netDetail : "proxy unreachable"); break;
    case NET_STALE:   snprintf(buf, sizeof buf, "stale data  -  %u live  -  %u games", live, g_gameCount); break;
    case NET_BOOT:    snprintf(buf, sizeof buf, "starting"); break;
    default:          snprintf(buf, sizeof buf, "%u live  -  %u games", live, g_gameCount); break;
  }
  if (strcmp(last, buf) == 0) return;
  strncpy(last, buf, sizeof last - 1);
  lv_label_set_text(s_lblStatus, buf);
}

bool uiShouldIdle() {
  for (uint8_t i = 0; i < g_gameCount; i++)
    if (g_board[i].state == GS_LIVE) return false;
  return true;
}

void uiShow(Screen s) {
  s_screen = s;
  // Flag writes are change-caching-exempt here because uiShow() is called on
  // navigation only, not per tick.
  if (s_board) (s == SCR_BOARD) ? lv_obj_clear_flag(s_board, LV_OBJ_FLAG_HIDDEN)
                                : lv_obj_add_flag(s_board, LV_OBJ_FLAG_HIDDEN);
  lv_obj_t* setup = uiSetupRoot();
  if (setup) (s == SCR_SETUP) ? lv_obj_clear_flag(setup, LV_OBJ_FLAG_HIDDEN)
                              : lv_obj_add_flag(setup, LV_OBJ_FLAG_HIDDEN);
  lv_obj_t* idle = uiIdleRoot();
  if (idle) (s == SCR_IDLE) ? lv_obj_clear_flag(idle, LV_OBJ_FLAG_HIDDEN)
                            : lv_obj_add_flag(idle, LV_OBJ_FLAG_HIDDEN);
}
Screen uiCurrent() { return s_screen; }

void uiInit() {
  lv_obj_t* scr = lv_scr_act();
  if (s_board) { lv_obj_del(s_board); s_board = nullptr; }

  s_board = lv_obj_create(scr);
  lv_obj_remove_style_all(s_board);
  lv_obj_set_size(s_board, SCR_W, SCR_H);
  lv_obj_set_pos(s_board, 0, 0);
  lv_obj_set_style_bg_color(s_board, C_PLATE, 0);
  lv_obj_set_style_bg_opa(s_board, LV_OPA_COVER, 0);
  lv_obj_clear_flag(s_board, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_board, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(s_board, onBoardPressed, LV_EVENT_SHORT_CLICKED, nullptr);

  buildBar();
  const uint8_t per = spec().cols * spec().rows;
  for (uint8_t i = 0; i < TILES_PER_PAGE; i++) {
    if (i < per) {
      buildTile(s_tile[i], i);
      lv_obj_add_flag(s_tile[i].root, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_event_cb(s_tile[i].root, onTilePressed, LV_EVENT_LONG_PRESSED, nullptr);
    } else {
      s_tile[i].root = nullptr;
      s_tile[i].cUsed = false;
    }
  }
  uiShow(s_screen);
}
