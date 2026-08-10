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
#include "../net/logos.h"
#include <time.h>

static Screen s_screen = SCR_BOARD;

static bool passesFilter(const Game& g);

static lv_obj_t* s_board;      // page root
static lv_obj_t* s_bar;
static lv_obj_t* s_lblClock;
static lv_obj_t* s_lblDate;
static lv_obj_t* s_lblStatus;
static lv_obj_t* s_lblPage;
static lv_obj_t* s_dot[8];
static uint8_t   s_dotCount;
// Whole empty grid rows carry content rather than nothing. A bordered empty
// region reads as "failed to load", not as "nothing to show" — and on a board
// that is usually only part full, that is most nights.
#define FILL_ROWS 5
static int       s_gridYOff;
static lv_obj_t* s_fill;
static lv_obj_t* s_fillHdr[2];
static lv_obj_t* s_fillRow[2][FILL_ROWS];

static lv_obj_t* s_toast;
static lv_obj_t* s_toastLbl;
static uint32_t  s_toastUntil;

#define CHIP_MAX (MAX_LEAGUES + 1)     // +1 for ALL
static lv_obj_t* s_chip[CHIP_MAX];
static lv_obj_t* s_chipLbl[CHIP_MAX];
static uint8_t   s_chipCount;

struct TileUI {
  lv_obj_t* root;
  lv_obj_t* edge;
  lv_obj_t* badge[2];
  lv_obj_t* badgeLbl[2];
  lv_obj_t* logo[2];
  const void* cLogo[2];
  lv_obj_t* abbr[2];
  lv_obj_t* rec[2];
  lv_obj_t* score[2];
  lv_obj_t* status;
  lv_obj_t* sit;
  lv_obj_t* bcast;
  // Field variants (LEADERBOARD / GRID) reuse the tile with a different shape:
  // an event title and three ranked rows instead of two sides and a score.
  lv_obj_t* fldTitle;
  lv_obj_t* fldPos[3];
  lv_obj_t* fldName[3];
  lv_obj_t* fldVal[3];
  // SET adds a row of per-set boxes under each player.
  lv_obj_t* setLbl[2];
  // change cache — the whole point of this struct
  char     cAbbr[2][20];   // holds an abbreviation OR a tennis name
  char     cRec[2][10];
  int32_t  cScore[2];
  uint32_t cColor[2];
  char     cStatus[16];
  char     cBcast[13];
  uint32_t cEdge;
  bool     cEdgeVis;
  int8_t   cState;
  char     cSit[14];
  bool     cSitVis;
  bool     cBcastVis;
  bool     cUsed;
  int8_t   gameIdx;   // index into g_board, -1 when the slot is empty
};
static TileUI s_tile[TILES_PER_PAGE];

// Which layout is actually in force. Auto reads the board: a light night gets
// bigger tiles because there is room, not because anything needs to be larger.
static uint8_t effectiveDensity() {
  if (g_set.density != DEN_AUTO) return g_set.density;
  uint8_t n = 0;
  for (uint8_t i = 0; i < g_gameCount; i++)
    if (passesFilter(g_board[i])) n++;
  if (n <= 6)  return DEN_ROOMY;
  if (n <= 9)  return DEN_STANDARD;
  return DEN_DENSE;
}
static const DensitySpec& spec() { return kDensity[effectiveDensity()]; }

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
  // Everything in a row hangs off one midline. Sizes here must track the font
  // scale in theme.cpp: the score face is 38 px, and positioning it as if it
  // were 28 left it riding high against the badge.
  const int scoreH = 38, abbrH = 17, recH = 11, textH = abbrH + 3 + recH;
  for (int i = 0; i < 2; i++) {
    const int ry  = TILE_PAD_Y + i * rowH;
    const int mid = ry + rowH / 2;

    t.badge[i] = lv_obj_create(t.root);
    lv_obj_remove_style_all(t.badge[i]);
    lv_obj_set_size(t.badge[i], d.badge, d.badge);
    lv_obj_set_pos(t.badge[i], TILE_PAD_X, mid - d.badge / 2);
    lv_obj_set_style_radius(t.badge[i], 7, 0);
    lv_obj_set_style_bg_opa(t.badge[i], LV_OPA_COVER, 0);
    // The followed-team mark. A ring rather than a coloured hairline: it is
    // independent of the team's own colour, it marks the TEAM rather than the
    // game (so a favourite-vs-favourite tie correctly shows two), and it does
    // not compete with the edge light for the tile perimeter.
    lv_obj_set_style_border_color(t.badge[i], C_INK, 0);
    lv_obj_set_style_border_opa(t.badge[i], LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(t.badge[i], 0, 0);
    lv_obj_clear_flag(t.badge[i], LV_OBJ_FLAG_SCROLLABLE);

    // Logos are 48 px on the wire and drawn smaller. Do NOT set an explicit
    // object size: in LVGL 8.3 that CLIPS the source rather than scaling it,
    // and the zoom then draws around a centre pivot — which is what made the
    // icons look cropped and offset from their row.
    //
    // SIZE_MODE_REAL makes the object take the zoomed size, and a 0,0 pivot
    // scales from the top-left into that box, so it lands exactly where the
    // colour badge would.
    t.logo[i] = lv_img_create(t.root);
    lv_img_set_antialias(t.logo[i], true);
    lv_img_set_zoom(t.logo[i], (uint16_t)((256 * d.badge) / 48));
    lv_img_set_pivot(t.logo[i], 0, 0);
    lv_img_set_size_mode(t.logo[i], LV_IMG_SIZE_MODE_REAL);
    lv_obj_set_pos(t.logo[i], TILE_PAD_X, mid - d.badge / 2);
    lv_obj_add_flag(t.logo[i], LV_OBJ_FLAG_HIDDEN);
    t.cLogo[i] = nullptr;

    t.badgeLbl[i] = lv_label_create(t.badge[i]);
    lv_obj_set_style_text_font(t.badgeLbl[i], F_MICRO, 0);
    lv_obj_set_style_text_color(t.badgeLbl[i], lv_color_white(), 0);
    lv_label_set_text(t.badgeLbl[i], "");
    lv_obj_center(t.badgeLbl[i]);

    const int tx = TILE_PAD_X + d.badge + 10;
    t.abbr[i] = microLabel(t.root, tx, mid - textH / 2, C_INK, F_ABBR);
    t.rec[i]  = microLabel(t.root, tx, mid - textH / 2 + abbrH + 3, C_INK3, F_NUM);

    t.score[i] = lv_label_create(t.root);
    lv_obj_set_style_text_font(t.score[i], F_SCORE, 0);
    lv_obj_set_style_text_color(t.score[i], C_INK, 0);
    lv_obj_set_style_text_align(t.score[i], LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(t.score[i], 74);
    lv_obj_set_pos(t.score[i], d.tileW - TILE_PAD_X - 74, mid - scoreH / 2);
    lv_label_set_text(t.score[i], "");
  }

  // ── field variant ────────────────────────────────────────────────────────
  t.fldTitle = microLabel(t.root, TILE_PAD_X, TILE_PAD_Y - 2, C_INK, F_BODY);  // mixed case
  lv_obj_set_width(t.fldTitle, d.tileW - 2 * TILE_PAD_X);
  lv_label_set_long_mode(t.fldTitle, LV_LABEL_LONG_DOT);
  for (int i = 0; i < 3; i++) {
    const int y = TILE_PAD_Y + 24 + i * 19;
    t.fldPos[i]  = microLabel(t.root, TILE_PAD_X, y, C_INK3, F_NUM);
    // Athlete names carry accents and lowercase — Raikkonen, Perez, Alcaraz.
    t.fldName[i] = microLabel(t.root, TILE_PAD_X + 26, y, C_INK2, F_BODY);
    lv_obj_set_width(t.fldName[i], d.tileW - 2 * TILE_PAD_X - 26 - 56);
    lv_label_set_long_mode(t.fldName[i], LV_LABEL_LONG_DOT);
    t.fldVal[i]  = lv_label_create(t.root);
    lv_obj_set_style_text_font(t.fldVal[i], F_NUM, 0);
    lv_obj_set_style_text_color(t.fldVal[i], C_INK, 0);
    lv_obj_set_style_text_align(t.fldVal[i], LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(t.fldVal[i], 54);
    lv_obj_set_pos(t.fldVal[i], d.tileW - TILE_PAD_X - 54, y);
    lv_label_set_text(t.fldVal[i], "");
  }
  // ── set boxes ────────────────────────────────────────────────────────────
  for (int i = 0; i < 2; i++) {
    t.setLbl[i] = lv_label_create(t.root);
    lv_obj_set_style_text_font(t.setLbl[i], F_NUM, 0);
    lv_obj_set_style_text_color(t.setLbl[i], C_INK2, 0);
    lv_obj_set_style_text_align(t.setLbl[i], LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(t.setLbl[i], 92);
    lv_obj_set_pos(t.setLbl[i], d.tileW - TILE_PAD_X - 92,
                   TILE_PAD_Y + i * ((d.tileH - 2 * TILE_PAD_Y - STATUS_H) / 2) + 8);
    lv_label_set_text(t.setLbl[i], "");
  }

  t.status = microLabel(t.root, TILE_PAD_X, d.tileH - TILE_PAD_Y - 15, C_INK2, F_NUM);
  t.bcast  = lv_label_create(t.root);
  lv_obj_set_style_text_font(t.bcast, F_NUM, 0);
  lv_obj_set_style_text_color(t.bcast, C_INK3, 0);
  lv_obj_set_style_text_align(t.bcast, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_width(t.bcast, 86);
  lv_obj_set_pos(t.bcast, d.tileW - TILE_PAD_X - 86, d.tileH - TILE_PAD_Y - 15);
  lv_label_set_text(t.bcast, "");

  // Situation: power play, red zone, bases loaded. The proxy has always sent
  // this and the device has always parsed it; nothing ever drew it. It sits in
  // the ~120 px of nothing between the status and the broadcast, and it is the
  // only thing on a tile that makes you keep watching rather than glance away.
  //
  // It shares the broadcast's slot rather than taking its own. The two are
  // never both worth showing: which channel a game is on matters before it
  // starts, and a man advantage only exists once it has. Sharing also means
  // neither can overrun the other, which they did when the chip had its own x.
  t.sit = lv_label_create(t.root);
  lv_obj_set_style_text_font(t.sit, F_NUM, 0);
  lv_obj_set_style_text_color(t.sit, C_INK, 0);
  lv_obj_set_style_text_align(t.sit, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_width(t.sit, 120);
  lv_obj_set_pos(t.sit, d.tileW - TILE_PAD_X - 120, d.tileH - TILE_PAD_Y - 15);
  lv_label_set_text(t.sit, "");
  lv_obj_add_flag(t.sit, LV_OBJ_FLAG_HIDDEN);

  memset(t.cAbbr, 0, sizeof t.cAbbr);
  memset(t.cRec, 0, sizeof t.cRec);
  memset(t.cStatus, 0, sizeof t.cStatus);
  memset(t.cBcast, 0, sizeof t.cBcast);
  t.cLogo[0] = t.cLogo[1] = nullptr;
  t.cScore[0] = t.cScore[1] = -1;
  t.cColor[0] = t.cColor[1] = 0xFFFFFFFF;
  t.cEdge = 0xFFFFFFFF;
  // MUST match the object's real state at build time (hidden), or the first
  // setHiddenCached(false) sees a match and returns without ever showing it.
  // A change-cache that disagrees with reality is worse than no cache: it
  // suppresses exactly the update it was meant to make cheap.
  t.cEdgeVis = false;
  t.cState = -1;
  t.cSit[0] = '\0';
  // Must match the objects' real state at build time — see the note above.
  t.cSitVis = false;
  t.cBcastVis = true;
  t.cUsed = true;
}

static void onChip(lv_event_t* e) {
  const int idx = (int)(intptr_t)lv_event_get_user_data(e);
  g_leagueFilter = (int8_t)(idx - 1);      // slot 0 is ALL
  g_page = 0;
  uiBoardRefresh();
}

/**
 * League strip. Chips carry a live count so the board tells you where the
 * action is before you filter to it. Tapping one filters; ALL clears.
 */
static void buildChips(lv_obj_t* bar) {
  int x = 250;
  for (uint8_t i = 0; i < CHIP_MAX; i++) {
    s_chip[i] = lv_obj_create(bar);
    lv_obj_remove_style_all(s_chip[i]);
    lv_obj_set_size(s_chip[i], 66, 28);
    lv_obj_set_pos(s_chip[i], x, (spec().barH - 28) / 2);
    lv_obj_set_style_radius(s_chip[i], 7, 0);
    lv_obj_set_style_bg_opa(s_chip[i], LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_chip[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_chip[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_chip[i], onChip, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    s_chipLbl[i] = lv_label_create(s_chip[i]);
    lv_obj_set_style_text_font(s_chipLbl[i], F_MICRO, 0);
    lv_obj_set_style_text_color(s_chipLbl[i], C_INK3, 0);
    lv_label_set_text(s_chipLbl[i], "");
    lv_obj_center(s_chipLbl[i]);
    lv_obj_add_flag(s_chip[i], LV_OBJ_FLAG_HIDDEN);
    x += 70;
  }
}

static void refreshChips() {
  s_chipCount = (uint8_t)min<int>(CHIP_MAX, g_leagueCount + 1);
  for (uint8_t i = 0; i < CHIP_MAX; i++) {
    if (i >= s_chipCount) { lv_obj_add_flag(s_chip[i], LV_OBJ_FLAG_HIDDEN); continue; }
    lv_obj_clear_flag(s_chip[i], LV_OBJ_FLAG_HIDDEN);

    char txt[20];
    if (i == 0) {
      uint8_t live = 0;
      for (uint8_t k = 0; k < g_gameCount; k++) if (g_board[k].state == GS_LIVE) live++;
      if (live) snprintf(txt, sizeof txt, "ALL %u", live);
      else      snprintf(txt, sizeof txt, "ALL");
    } else {
      const LeagueCount& lc = g_leagues[i - 1];
      char up[8];
      strncpy(up, lc.slug, sizeof up - 1); up[sizeof up - 1] = '\0';
      for (char* p = up; *p; p++) *p = toupper((unsigned char)*p);
      if (lc.live) snprintf(txt, sizeof txt, "%s %u", up, lc.live);
      else         snprintf(txt, sizeof txt, "%s", up);
    }
    lv_label_set_text(s_chipLbl[i], txt);

    const bool sel = (g_leagueFilter + 1) == (int)i;
    lv_obj_set_style_bg_opa(s_chip[i], sel ? 40 : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(s_chip[i], C_EDGE_HI, 0);
    lv_obj_set_style_text_color(s_chipLbl[i], sel ? C_INK : C_INK3, 0);
  }
}

/** A 44x36 bar button. Small enough to fit beside the chips, big enough to
 *  hit — the ISO 9241-411 floor is 9 mm, which is 47 px on this panel, and
 *  these sit at 44 so the label stays legible. */
static lv_obj_t* barButton(lv_obj_t* bar, int x, const char* text, lv_event_cb_t cb) {
  lv_obj_t* b = lv_btn_create(bar);
  lv_obj_set_size(b, 44, 36);
  lv_obj_set_pos(b, x, (spec().barH - 36) / 2);
  lv_obj_set_style_bg_color(b, C_EDGE, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(b, C_EDGE_HI, 0);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_radius(b, 8, 0);
  lv_obj_set_style_bg_color(b, C_EDGE_HI, LV_STATE_PRESSED);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* l = lv_label_create(b);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, F_MICRO, 0);
  lv_obj_set_style_text_color(l, C_INK2, 0);
  lv_obj_center(l);
  return b;
}

static void onTableBtn(lv_event_t*) {
  // The filtered league if one is chosen, else whichever league has the most
  // going on — which is nearly always the one you meant.
  const char* lg = (g_leagueFilter >= 0 && g_leagueFilter < g_leagueCount)
                   ? g_leagues[g_leagueFilter].slug
                   : (g_leagueCount ? g_leagues[0].slug : "nhl");
  uiStandingsOpen(lg);
}
static void onNewsBtn(lv_event_t*) { uiNewsOpen(); }
static void onSettingsBtn(lv_event_t*) { uiSettingsOpen(); }

static void buildBar() {
  s_bar = glassPanel(s_board, 0, 0, SCR_W, spec().barH, 0);
  s_lblClock = microLabel(s_bar, 18, 12, C_INK, F_ABBR);
  s_lblDate  = microLabel(s_bar, 84, 17, C_INK2, F_MICRO);

  // Standings and news were built, tested, and reachable only from a serial
  // command and the desktop harness — two finished screens shipping dark.
  barButton(s_bar, SCR_W - 18 - 44, "SET",  onSettingsBtn);
  barButton(s_bar, SCR_W - 18 - 96, "NEWS", onNewsBtn);
  barButton(s_bar, SCR_W - 18 - 148, "TBL", onTableBtn);

  s_lblStatus = lv_label_create(s_bar);
  lv_obj_set_style_text_font(s_lblStatus, F_MICRO, 0);
  lv_obj_set_style_text_color(s_lblStatus, C_INK2, 0);
  lv_obj_set_style_text_align(s_lblStatus, LV_TEXT_ALIGN_RIGHT, 0);
  // Was 190 wide at x = SCR_W-208, which the stale-upstream string overran and
  // wrapped out of the bar. Narrower, and it now only carries network state —
  // the live/game counts already live in the ALL chip.
  lv_obj_set_width(s_lblStatus, 150);
  lv_obj_set_pos(s_lblStatus, SCR_W - 174 - 150, 17);
  lv_label_set_text(s_lblStatus, "starting");

  buildChips(s_bar);

  // Page indicator: real dots, in the bar. It used to be "*  -  -  -" in a
  // mono face, sitting in a 12 px sliver at the foot of the screen where the
  // bottom margin breaks the grid's 16 px rhythm — ASCII punctuation pressed
  // into service as a widget.
  for (uint8_t i = 0; i < 8; i++) {
    s_dot[i] = lv_obj_create(s_bar);
    lv_obj_remove_style_all(s_dot[i]);
    lv_obj_set_size(s_dot[i], 6, 6);
    lv_obj_set_style_radius(s_dot[i], 3, 0);
    lv_obj_set_style_bg_opa(s_dot[i], LV_OPA_COVER, 0);
    lv_obj_add_flag(s_dot[i], LV_OBJ_FLAG_HIDDEN);
  }
  s_lblPage = nullptr;

  // The filler. Two columns: what has finished, and what is next.
  s_fill = glassPanel(s_board, 0, 0, 10, 10, 12);
  lv_obj_add_flag(s_fill, LV_OBJ_FLAG_HIDDEN);
  for (int c = 0; c < 2; c++) {
    s_fillHdr[c] = lv_label_create(s_fill);
    lv_obj_set_style_text_font(s_fillHdr[c], F_MICRO, 0);
    lv_obj_set_style_text_color(s_fillHdr[c], C_INK3, 0);
    lv_label_set_text(s_fillHdr[c], c == 0 ? "FINAL" : "NEXT UP");
    for (int r = 0; r < FILL_ROWS; r++) {
      s_fillRow[c][r] = lv_label_create(s_fill);
      lv_obj_set_style_text_font(s_fillRow[c][r], F_NUM, 0);
      lv_obj_set_style_text_color(s_fillRow[c][r], C_INK2, 0);
      lv_label_set_text(s_fillRow[c][r], "");
    }
  }

  // Toast: one line, centred, 1.2 s. Built once and reused.
  s_toast = glassPanel(s_board, (SCR_W - 220) / 2, SCR_H - 78, 220, 42, 10);
  lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
  s_toastLbl = lv_label_create(s_toast);
  lv_obj_set_style_text_font(s_toastLbl, F_ABBR, 0);
  lv_obj_set_style_text_color(s_toastLbl, C_INK, 0);
  lv_label_set_text(s_toastLbl, "");
  lv_obj_center(s_toastLbl);
}

void uiToast(const char* text) {
  if (!s_toast) return;
  lv_label_set_text(s_toastLbl, text);
  lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(s_toast);
  s_toastUntil = millis() + 1200;
}

// ── score flare ────────────────────────────────────────────────────────────
// The non-occluding half of an alert: the scoring team's tile widens and
// brightens its edge light for a couple of seconds. Nothing is covered, and
// the whole effect is 3 style writes on one object.
static int8_t   s_flashTile = -1;
static uint32_t s_flashUntil;

void uiBoardFlash(const char* gameId) {
  for (uint8_t i = 0; i < TILES_PER_PAGE; i++) {
    TileUI& t = s_tile[i];
    if (!t.root || t.gameIdx < 0 || t.gameIdx >= g_gameCount) continue;
    if (strcmp(g_board[t.gameIdx].id, gameId) != 0) continue;
    lv_obj_set_width(t.edge, EDGE_W * 3);
    lv_obj_clear_flag(t.edge, LV_OBJ_FLAG_HIDDEN);
    t.cEdgeVis = true;
    s_flashTile = (int8_t)i;
    s_flashUntil = millis() + 2000;
    return;
  }
}

static void flashTick() {
  if (s_flashTile < 0 || millis() < s_flashUntil) return;
  TileUI& t = s_tile[s_flashTile];
  if (t.root) lv_obj_set_width(t.edge, EDGE_W);
  s_flashTile = -1;
}

void uiToastTick() {
  flashTick();
  if (!s_toast || !s_toastUntil) return;
  if (millis() >= s_toastUntil) {
    s_toastUntil = 0;
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
  }
}

// ── paging / filter ────────────────────────────────────────────────────────
static bool passesFilter(const Game& g) {
  if (g_leagueFilter < 0 || g_leagueFilter >= g_leagueCount) return true;
  return strcmp(g.league, g_leagues[g_leagueFilter].slug) == 0;
}

static uint8_t visibleCount() {
  uint8_t n = 0;
  for (uint8_t i = 0; i < g_gameCount; i++) if (passesFilter(g_board[i])) n++;
  return n;
}

// ── ordering ───────────────────────────────────────────────────────────────
//
// The board used to render g_board in whatever order the proxy sent, which on
// a 48-game Saturday put your own team wherever it happened to land — possibly
// four pages in, looking exactly like every other tile. For a personal
// scoreboard that is the deepest defect there is.
//
// An index array, insertion-sorted. No allocation, stable, and 48 entries is
// nothing. Ties keep proxy order, which is already roughly by start time.
static uint8_t s_order[MAX_GAMES];

static uint8_t rankOf(const Game& g) {
  switch (g.state) {
    case GS_LIVE:  return g.isFav ? 0 : 1;
    case GS_PRE:   return g.isFav ? 2 : 3;
    default:       return g.isFav ? 4 : 5;
  }
}

static void buildOrder() {
  for (uint8_t i = 0; i < g_gameCount; i++) s_order[i] = i;
  for (uint8_t i = 1; i < g_gameCount; i++) {
    const uint8_t v = s_order[i];
    const uint8_t rv = rankOf(g_board[v]);
    int j = (int)i - 1;
    while (j >= 0 && rankOf(g_board[s_order[j]]) > rv) {
      s_order[j + 1] = s_order[j];
      j--;
    }
    s_order[j + 1] = v;
  }
}

bool uiBoardPage(int delta) {
  const uint8_t per = spec().cols * spec().rows;
  const uint8_t pages = (visibleCount() + per - 1) / per;
  if (pages <= 1) return false;
  const int next = (int)g_page + delta;
  g_page = (uint8_t)((next % pages + pages) % pages);
  uiBoardRefresh();
  return true;
}

static void onTileEvent(lv_event_t* e) {
  const lv_event_code_t code = lv_event_get_code(e);
  TileUI* t = (TileUI*)lv_event_get_user_data(e);
  if (!t) return;

  if (code == LV_EVENT_LONG_PRESSED) {
    // Density has no settings screen yet; long-press keeps it reachable.
    // It used to change silently — an accidental long-press reformatted the
    // whole board with no explanation and nothing to undo it with.
    g_set.density = (g_set.density + 1) % DEN_COUNT;
    settingsSave();
    uiInit();
    uiBoardRefresh();
    static const char* kName[DEN_COUNT] = { "ROOMY", "STANDARD", "DENSE", "AUTO" };
    uiToast(kName[g_set.density]);
    return;
  }
  if (code == LV_EVENT_SHORT_CLICKED) {
    if (t->gameIdx >= 0 && t->gameIdx < g_gameCount) uiGameOpen(g_board[t->gameIdx]);
  }
}

/**
 * Swipe paging.
 *
 * The tiles cover almost the whole board and are clickable, so they swallow
 * taps before the background ever sees them — which is why tapping to page
 * did not work. LVGL raises GESTURE on the *indev's* active object, so the
 * handler has to live on every tile as well as the background, and each one
 * must clear the scroll-propagation flag or the gesture is eaten too.
 */
static void onGesture(lv_event_t*) {
  const lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
  if (dir == LV_DIR_LEFT)  uiBoardPage(+1);
  else if (dir == LV_DIR_RIGHT) uiBoardPage(-1);
}

// ── refresh ────────────────────────────────────────────────────────────────
/**
 * Cover the whole empty rows at the foot of the grid with one panel of real
 * content. Only WHOLE rows: a filler that starts mid-row would sit in the
 * grid's rhythm wrongly and look like a tile that had grown.
 */
static void layoutFiller(const DensitySpec& d, uint8_t used, uint8_t per) {
  const uint8_t usedRows = (used + d.cols - 1) / d.cols;
  const uint8_t freeRows = d.rows > usedRows ? d.rows - usedRows : 0;
  if (freeRows == 0 || used == 0) { lv_obj_add_flag(s_fill, LV_OBJ_FLAG_HIDDEN); return; }

  const int y = d.gridTop + usedRows * (d.tileH + d.gut) + s_gridYOff;
  const int hMax = freeRows * d.tileH + (freeRows - 1) * d.gut;
  const int w = d.cols * d.tileW + (d.cols - 1) * d.gut;
  const int colW = w / 2;
  const int rowsFit = (hMax - 46) / 22;
  const int nRows = rowsFit < FILL_ROWS ? (rowsFit < 0 ? 0 : rowsFit) : FILL_ROWS;

  for (int c = 0; c < 2; c++) {
    lv_obj_set_pos(s_fillHdr[c], 16 + c * colW, 12);
    for (int r = 0; r < FILL_ROWS; r++) {
      lv_obj_set_pos(s_fillRow[c][r], 16 + c * colW, 34 + r * 22);
      lv_label_set_text(s_fillRow[c][r], "");
      if (r >= nRows) lv_obj_add_flag(s_fillRow[c][r], LV_OBJ_FLAG_HIDDEN);
      else            lv_obj_clear_flag(s_fillRow[c][r], LV_OBJ_FLAG_HIDDEN);
    }
  }

  char buf[48];
  int fin = 0, nxt = 0;
  for (uint8_t oi = 0; oi < g_gameCount; oi++) {
    const uint8_t gi = s_order[oi];
    const Game& g = g_board[gi];
    if (!passesFilter(g)) continue;
    // Anything already on a tile must not appear twice.
    bool onTile = false;
    for (uint8_t t = 0; t < used; t++) if (s_tile[t].gameIdx == (int8_t)gi) { onTile = true; break; }
    if (onTile) continue;
    if (g.state == GS_FINAL && fin < nRows) {
      snprintf(buf, sizeof buf, "%-4s %2u   %-4s %2u",
               g.away.abbr, g.away.score, g.home.abbr, g.home.score);
      lv_label_set_text(s_fillRow[0][fin++], buf);
    } else if (g.state == GS_PRE && nxt < nRows) {
      snprintf(buf, sizeof buf, "%-7s %s @ %s", g.status, g.away.abbr, g.home.abbr);
      lv_label_set_text(s_fillRow[1][nxt++], buf);
    }
  }
  // An empty panel is worse than empty space: a bordered region with nothing
  // in it is exactly what "failed to load" looks like.
  if (!fin && !nxt) { lv_obj_add_flag(s_fill, LV_OBJ_FLAG_HIDDEN); return; }

  // Height follows the content, not the hole — a half-empty panel is the same
  // "something is missing" signal in a smaller size.
  const int rows = fin > nxt ? fin : nxt;
  int h = 46 + rows * 22;
  if (h > hMax) h = hMax;
  lv_obj_set_pos(s_fill, d.marg, y);
  lv_obj_set_size(s_fill, w, h);
  lv_obj_clear_flag(s_fill, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_opa(s_fillHdr[0], fin ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  lv_obj_set_style_opa(s_fillHdr[1], nxt ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
}

void uiBoardRefresh() {
  const DensitySpec& d = spec();
  const uint8_t per = d.cols * d.rows;
  const uint8_t pages = max<uint8_t>(1, (visibleCount() + per - 1) / per);
  if (g_page >= pages) g_page = 0;

  buildOrder();
  uint8_t seen = 0, slot = 0;
  for (uint8_t oi = 0; oi < g_gameCount && slot < per; oi++) {
    const uint8_t i = s_order[oi];
    const Game& g = g_board[i];
    if (!passesFilter(g)) continue;
    if (seen++ < g_page * per) continue;

    TileUI& t = s_tile[slot];
    t.gameIdx = (int8_t)i;
    setHiddenCached(t.root, &t.cUsed, false);

    // State ink. This used to be one opacity on the tile root, which faded the
    // frost and the text together and took a final's record and broadcast to
    // 1.73:1 — so a finished game read as a tile that had failed to load. It
    // also pushed every tile through a 63 KB composite buffer.
    const StateInk& si = kStateInk[g.state];
    if (t.cState != g.state) {
      t.cState = g.state;
      lv_obj_set_style_bg_color(t.root, si.plate, 0);
      lv_obj_set_style_border_color(t.root, si.edge, 0);
      for (int k = 0; k < 2; k++) {
        lv_obj_set_style_text_color(t.abbr[k], si.ink, 0);
        lv_obj_set_style_text_color(t.rec[k],  si.ink3, 0);
      }
      lv_obj_set_style_text_color(t.status, si.ink2, 0);
      lv_obj_set_style_text_color(t.bcast,  si.ink3, 0);
      lv_obj_set_style_text_color(t.fldTitle, si.ink, 0);
    }

    // Three models do not have two sides and a score. Show/hide the two
    // shapes rather than building separate tiles: the grid, the frost and the
    // edge light are identical, only the contents differ.
    const bool isField = (g.model == SM_LEADERBOARD || g.model == SM_GRID);
    const bool isSet   = (g.model == SM_SET);
    lv_obj_t* const twoSided[] = { t.badge[0], t.badge[1], t.abbr[0], t.abbr[1],
                                   t.rec[0], t.rec[1], t.score[0], t.score[1] };
    for (lv_obj_t* o : twoSided)
      isField ? lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN) : lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t* const fieldOnly[] = { t.fldTitle, t.fldPos[0], t.fldPos[1], t.fldPos[2],
                                    t.fldName[0], t.fldName[1], t.fldName[2],
                                    t.fldVal[0], t.fldVal[1], t.fldVal[2] };
    for (lv_obj_t* o : fieldOnly)
      isField ? lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);

    if (isField) {
      lv_label_set_text(t.fldTitle, g.away.name);
      const FieldSet* F = (g.fieldIdx >= 0 && g.fieldIdx < FLD_POOL) ? &g_fields[g.fieldIdx] : nullptr;
      for (int k = 0; k < 3; k++) {
        const bool on = F && k < F->count;
        lv_label_set_text(t.fldPos[k],  on ? F->rows[k].pos : "");
        lv_label_set_text(t.fldName[k], on ? F->rows[k].name : (k == 0 && !F ? g.home.name : ""));
        lv_label_set_text(t.fldVal[k],  on ? F->rows[k].val : "");
      }
      setTextCached(t.status, t.cStatus, sizeof t.cStatus, g.status);
      setTextCached(t.bcast,  t.cBcast,  sizeof t.cBcast,  g.bcast);
      // State ink already applied above; a leaderboard has no leader side.
      setHiddenCached(t.sit,   &t.cSitVis,   true);
      setHiddenCached(t.bcast, &t.cBcastVis, false);
      setHiddenCached(t.edge, &t.cEdgeVis, true);
      slot++;
      continue;
    }

    const Side* side[2] = { &g.away, &g.home };
    for (int k = 0; k < 2; k++) {
      // Tennis needs "B. Shelton", which does not fit the 4-char abbr field —
      // the proxy puts the readable form in `name` for SET. It also needs a
      // different FACE: F_ABBR has no lowercase glyphs (see theme.h).
      lv_obj_set_style_text_font(t.abbr[k], isSet ? F_BODY : F_ABBR, 0);
      setTextCached(t.abbr[k], t.cAbbr[k], sizeof t.cAbbr[k],
                    isSet ? side[k]->name : side[k]->abbr);
      setTextCached(t.rec[k],  t.cRec[k],  sizeof t.cRec[k],  side[k]->rec);
      if (g.state == GS_PRE) {
        if (t.cScore[k] != -2) { t.cScore[k] = -2; lv_label_set_text(t.score[k], "-"); }
      } else {
        setNumCached(t.score[k], &t.cScore[k], side[k]->score);
      }
      // The losing side drops to ink-2 — the second half of the state channel.
      const bool leading = (g.state != GS_PRE) &&
                           ((k == 1) == g.leaderHome) && (g.away.score != g.home.score);
      lv_obj_set_style_text_color(t.score[k], leading ? si.ink : si.ink2, 0);

      // Followed teams keep a ring on the badge — see buildTile().
      const bool fav = sideIsFav(g.league, side[k]->id);
      lv_obj_set_style_border_width(t.badge[k], fav ? 2 : 0, 0);

      if (t.cColor[k] != side[k]->color) {
        t.cColor[k] = side[k]->color;
        teamBadgeSet(t.badge[k], side[k]->color);
      }
      lv_label_set_text(t.badgeLbl[k], side[k]->abbr);

      // Logo when we have one, colour badge otherwise. Change-cached: setting
      // the same source still repaints, and that is what fights the panel DMA.
      const lv_img_dsc_t* img = logoGet(g.league, side[k]->abbr);
      if (t.cLogo[k] != (const void*)img) {
        t.cLogo[k] = img;
        if (img) {
          lv_img_set_src(t.logo[k], img);
          lv_obj_clear_flag(t.logo[k], LV_OBJ_FLAG_HIDDEN);
          lv_obj_add_flag(t.badge[k], LV_OBJ_FLAG_HIDDEN);
        } else {
          lv_obj_add_flag(t.logo[k], LV_OBJ_FLAG_HIDDEN);
          lv_obj_clear_flag(t.badge[k], LV_OBJ_FLAG_HIDDEN);
        }
      }
    }

    // Tennis: the per-set scores go where the record line sits, so they never
    // collide with the score, and a club record is not invented for a player.
    for (int k = 0; k < 2; k++) lv_obj_add_flag(t.setLbl[k], LV_OBJ_FLAG_HIDDEN);
    if (isSet) {
      for (int k = 0; k < 2; k++) {
        char sb[32] = "";
        char* w = sb;
        for (uint8_t si = 0; si < g.setCount && (w - sb) < 24; si++)
          w += snprintf(w, sizeof sb - (w - sb), "%u  ",
                        k == 0 ? g.setsAway[si] : g.setsHome[si]);
        lv_label_set_text(t.rec[k], sb);
      }
    }

    setTextCached(t.status, t.cStatus, sizeof t.cStatus, g.status);
    setTextCached(t.bcast,  t.cBcast,  sizeof t.cBcast,  g.bcast);

    char sit[14] = "";
    situationText(g, sit, sizeof sit);
    setHiddenCached(t.sit,   &t.cSitVis,   !sit[0]);
    setHiddenCached(t.bcast, &t.cBcastVis,  sit[0] != '\0');
    if (sit[0]) setTextCached(t.sit, t.cSit, sizeof t.cSit, sit);
    else        t.cSit[0] = '\0';

    // Edge light: only live games, on the leading side.
    const bool edgeOn = (g.state == GS_LIVE);
    setHiddenCached(t.edge, &t.cEdgeVis, !edgeOn);
    if (edgeOn) {
      // teamInk, not the raw colour: Toronto navy against the tile is 1.11:1,
      // which made the product's signature element invisible on its own
      // flagship example. See theme.cpp.
      const uint32_t c = teamInk(g.leaderHome ? g.home.color : g.away.color);
      if (t.cEdge != c) { t.cEdge = c; lv_obj_set_style_bg_color(t.edge, lv_color_hex(c), 0); }
      lv_obj_set_x(t.edge, g.leaderHome ? d.tileW - EDGE_W - 1 : 0);
    }
    slot++;
  }

  const uint8_t filled = slot;      // capture BEFORE the hide loop advances it

  // Centre a part-full grid vertically. Top-aligning it leaves the whole lower
  // half as one void, which reads as content that failed to arrive; splitting
  // the slack above and below reads as a composition. Cached, because this is
  // a position write on every tile.
  const uint8_t usedRows = filled ? (filled + d.cols - 1) / d.cols : 0;
  int yOff = 0;
  if (usedRows && usedRows < d.rows) {
    const int freeH = (d.rows - usedRows) * (d.tileH + d.gut);
    yOff = freeH / 2;
  }
  if (yOff != s_gridYOff) {
    s_gridYOff = yOff;
    for (uint8_t i = 0; i < TILES_PER_PAGE; i++) {
      if (!s_tile[i].root) continue;
      const int r = i / d.cols, c = i % d.cols;
      lv_obj_set_pos(s_tile[i].root, d.marg + c * (d.tileW + d.gut),
                     d.gridTop + r * (d.tileH + d.gut) + yOff);
    }
  }
  for (; slot < per; slot++) {
    s_tile[slot].gameIdx = -1;
    setHiddenCached(s_tile[slot].root, &s_tile[slot].cUsed, true);
  }
  layoutFiller(d, filled, per);

  // Dots read as "there is more" far better than "1 / 3" does.
  const uint8_t shown = pages > 1 ? (pages < 8 ? pages : 8) : 0;
  const int dotsW = shown ? shown * 14 - 8 : 0;
  for (uint8_t p = 0; p < 8; p++) {
    if (p >= shown) { lv_obj_add_flag(s_dot[p], LV_OBJ_FLAG_HIDDEN); continue; }
    lv_obj_clear_flag(s_dot[p], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(s_dot[p], 196 - dotsW / 2 + p * 14, (spec().barH - 6) / 2);
    lv_obj_set_style_bg_color(s_dot[p], p == g_page ? C_INK : C_EDGE_HI, 0);
  }
  refreshChips();
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

  // Network state ONLY. The live and game counts are already in the ALL chip,
  // and carrying them here is what overran the label on the stale path.
  //
  // A hyphen between two numbers reads as a score on a scoreboard, so the
  // separator throughout is a middle dot.
  char buf[96];
  switch (g_net) {
    case NET_NOWIFI:  snprintf(buf, sizeof buf, "no wi-fi"); break;
    case NET_NOPROXY: snprintf(buf, sizeof buf, "no proxy configured"); break;
    case NET_ERR:     snprintf(buf, sizeof buf, "%s", g_netDetail[0] ? g_netDetail : "proxy unreachable"); break;
    // "stale" is a state; a time is actionable. Say when the data is from.
    case NET_STALE: {
      const char* t = lastGoodClock();
      if (t[0]) snprintf(buf, sizeof buf, "as of %s", t);
      else      snprintf(buf, sizeof buf, "upstream stale");
      break;
    }
    case NET_BOOT:    snprintf(buf, sizeof buf, "starting"); break;
    default:          buf[0] = '\0'; break;
  }
  (void)live;
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
  lv_obj_t* game = uiGameRoot();
  if (game) (s == SCR_GAME) ? lv_obj_clear_flag(game, LV_OBJ_FLAG_HIDDEN)
                            : lv_obj_add_flag(game, LV_OBJ_FLAG_HIDDEN);
  lv_obj_t* st = uiStandingsRoot();
  if (st) (s == SCR_STANDINGS) ? lv_obj_clear_flag(st, LV_OBJ_FLAG_HIDDEN)
                               : lv_obj_add_flag(st, LV_OBJ_FLAG_HIDDEN);
  lv_obj_t* nw = uiNewsRoot();
  if (nw) (s == SCR_NEWS) ? lv_obj_clear_flag(nw, LV_OBJ_FLAG_HIDDEN)
                          : lv_obj_add_flag(nw, LV_OBJ_FLAG_HIDDEN);
  lv_obj_t* lu = uiLineupRoot();
  if (lu) (s == SCR_LINEUP) ? lv_obj_clear_flag(lu, LV_OBJ_FLAG_HIDDEN)
                            : lv_obj_add_flag(lu, LV_OBJ_FLAG_HIDDEN);
  lv_obj_t* st2 = uiSettingsRoot();
  if (st2) (s == SCR_SETTINGS) ? lv_obj_clear_flag(st2, LV_OBJ_FLAG_HIDDEN)
                               : lv_obj_add_flag(st2, LV_OBJ_FLAG_HIDDEN);
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
  lv_obj_add_event_cb(s_board, onGesture, LV_EVENT_GESTURE, nullptr);

  buildBar();
  const uint8_t per = spec().cols * spec().rows;
  for (uint8_t i = 0; i < TILES_PER_PAGE; i++) {
    if (i < per) {
      buildTile(s_tile[i], i);
      lv_obj_add_flag(s_tile[i].root, LV_OBJ_FLAG_CLICKABLE);
      // Without this the tile absorbs the gesture and swiping does nothing.
      lv_obj_clear_flag(s_tile[i].root, LV_OBJ_FLAG_GESTURE_BUBBLE);
      lv_obj_add_event_cb(s_tile[i].root, onTileEvent, LV_EVENT_SHORT_CLICKED, &s_tile[i]);
      lv_obj_add_event_cb(s_tile[i].root, onTileEvent, LV_EVENT_LONG_PRESSED, &s_tile[i]);
      lv_obj_add_event_cb(s_tile[i].root, onGesture, LV_EVENT_GESTURE, nullptr);
    } else {
      s_tile[i].root = nullptr;
      s_tile[i].cUsed = false;
    }
  }
  uiShow(s_screen);
}
