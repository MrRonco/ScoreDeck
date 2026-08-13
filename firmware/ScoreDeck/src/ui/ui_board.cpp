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

// Which density the tile array was BUILT for. Auto density is computed from
// the game count, so it can change between uiInit() and a later refresh — and
// uiInit() only creates `cols * rows` tiles. Booting with an empty board built
// six Roomy tiles; twelve games then arrived, density became Dense, and the
// refresh walked into six tiles that had never been created. Null root,
// LoadProhibited, panic.
static uint8_t s_builtDensity = 0xFF;
static bool    s_builtRail;      // rail state the tile array was built for

static lv_obj_t* s_board;      // page root
static lv_obj_t* s_bar;
static lv_obj_t* s_lblPage;
static lv_obj_t* s_dot[8];
static uint8_t   s_dotCount;

// ── the refreshed header ───────────────────────────────────────────────────
// Three zones with jobs (refresh-spec.md §2): the live organ, the filter
// pill (the rail's opener), and the state+nav zone. The old header spent its
// best real estate on the clock, never showed the accent, and carried a
// status label that could print over chip 4 — all three retired here.
static lv_obj_t* s_zaDot;      // pulse-registered
static lv_obj_t* s_zaLive;     // "LIVE" / "NO GAMES LIVE"
static lv_obj_t* s_zaCount;    // the first bright thing on the panel
static lv_obj_t* s_zaTotal;    // "/ 9" — the denominator
static lv_obj_t* s_pill;       // zone B: names the filter, opens the rail
static lv_obj_t* s_pillLbl;
static lv_obj_t* s_pillUnder;  // C_LIVE_SD underline when the filter has live
static lv_obj_t* s_zc;         // zone C slot: clock / "+N NEW" / trouble
static lv_obj_t* s_zcLbl;
static lv_obj_t* s_navNewsDot;
static char      s_clockStr[12] = "";   // "12:35 AM" is 8 chars + NUL — 8 truncated it
static char      s_zcCache[52] = "\x01"; // zone C's change cache; reset by buildBar
static char      c_zaCount[6], c_zaTotal[10], c_pill[30];
// The delta ledger: scores that changed while you were not looking.
static uint32_t  s_lastTouchMs;
static uint8_t   s_deltaCount;
static uint32_t  s_deltaUntil;
// The poll heartbeat lives on two bars with two LIFETIMES: slot 0 is the
// board's line and dies with every uiInit() rebuild; slot 1 is the idle
// bar's and lives forever (uiIdleInit runs once at boot). A shared counter
// reset by uiInit orphaned the idle line after the first rebuild — on the
// screen that is up most of the day. Review r1, blocker 2.
static lv_obj_t* s_heart[2];
static int       c_heartW = -1;
static bool      c_heartWarn;
// Whole empty grid rows carry content rather than nothing. A bordered empty
// region reads as "failed to load", not as "nothing to show" — and on a board
// that is usually only part full, that is most nights.
// F_NUM is monospaced at exactly 9.0 px/glyph. 81 px is nine glyphs, which
// covers every in-play status ("3rd 04:21", "Bot 8th", "Final/OT").
//
// It does NOT cover every status: Game::status is char[15], and F1 sends
// "8/21 - 6:30 AM" at fourteen. So this is the reserve held back only while
// something is actually sharing the row — when the right-hand label is empty
// the status takes the whole width, which is what a scheduled race needs.
#define STATUS_W  81
// The widest full-vocabulary situation string, "BASES LOADED", is 12 glyphs.
// A right-hand column narrower than this gets the compact vocabulary instead.
#define SIT_FULL_W 108

#define FILL_ROWS 5
static int       s_gridYOff;
static lv_obj_t* s_fill;
static lv_obj_t* s_fillHdr[2];
static lv_obj_t* s_fillRow[2][FILL_ROWS];

static lv_obj_t* s_toast;
static lv_obj_t* s_toastLbl;
static uint32_t  s_toastUntil;



struct TileUI {
  lv_obj_t* root;
  lv_obj_t* edge;
  lv_obj_t* badge[2];
  lv_obj_t* favRing[2];
  bool      cFav[2];
  lv_obj_t* badgeLbl[2];
  lv_obj_t* logo[2];
  char     cLogoKey[2][10];   // league:abbr the logo was set FOR
  bool     cLogoVis[2];
  bool     cBadgeVis[2];
  lv_obj_t* abbr[2];
  lv_obj_t* rec[2];
  lv_obj_t* score[2];
  lv_obj_t* status;
  lv_obj_t* dot;      // the live accent
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
  char     cSit[20];   // widest: three 3-byte diamonds + " 2 OUT"
  bool     cSitVis;
  bool     cBcastVis;
  bool     cDotVis;
  int8_t   cShape;     // -1 unknown, 0 two-sided, 1 field — gates the vis swap
  uint32_t cScoreInk[2];
  uint32_t cChip[2];    // packed: 0 none, 1 logo+no chip, else colour|0x1000000
  int16_t  cStatusW;
  bool     cLead;      // slot-0 lead treatment applied
  bool     cUsed;
  int8_t   gameIdx;   // index into g_board, -1 when the slot is empty
};
static TileUI s_tile[TILES_PER_PAGE];

// Which layout is actually in force.
//
// AUTO used to read only the game COUNT, and give a light night bigger tiles
// because there was room. That is backwards: a quiet night is not a night that
// needs larger type, it is a night with one game worth looking at. So AUTO now
// reads the SHAPE of the board — when between one and three games are live,
// there is something to promote, and it picks the featured layout. Four or
// more live and nothing deserves a hero, so the grid is the honest answer.
static uint8_t liveVisible() {
  uint8_t n = 0;
  for (uint8_t i = 0; i < g_gameCount; i++)
    if (g_board[i].state == GS_LIVE && passesFilter(g_board[i])) n++;
  return n;
}

static uint8_t effectiveDensity() {
  if (g_set.density != DEN_AUTO) return g_set.density;
  uint8_t n = 0;
  for (uint8_t i = 0; i < g_gameCount; i++)
    if (passesFilter(g_board[i])) n++;
  const uint8_t live = liveVisible();
  if (live >= 1 && live <= 3) return LAY_FEATURE;
  if (n <= 6)  return DEN_ROOMY;
  if (n <= 9)  return DEN_STANDARD;
  return DEN_DENSE;
}
static bool isFeature() { return effectiveDensity() == LAY_FEATURE; }

/**
 * The layout in force — BY VALUE, because the rail rewrites geometry that
 * kDensity's rows cannot carry. Two overrides:
 *
 *   * closed rail: the left margin clamps to 16 so the sliver's own strip
 *     (0..16) never underlaps a tile;
 *   * open rail:   cols' = min(cols, 3), W' = (632 − 10·(cols'−1)) / cols'
 *     = 204 px at 3 columns — WIDER than Dense's 186, so nothing degrades —
 *     with the grid origin moved past the rail.
 */
static DensitySpec spec() {
  const uint8_t e = effectiveDensity();
  DensitySpec d = kDensity[e];
  if (uiRailOpen()) {
    if (e == LAY_FEATURE) {
      // The narrowed-hero variant: hero 430 at x=156 (ui_hero derives its own
      // internals from the rail state), tile strip at x=596, 192 wide — the
      // one column the remaining 204 px minus a gutter can hold.
      d.marg = 596;
      d.tileW = 192;
    } else {
      d.cols = d.cols < 3 ? d.cols : 3;
      d.gut  = 10;
      d.tileW = (uint16_t)((632 - 10 * (d.cols - 1)) / d.cols);
      d.marg = 156;                     // 140 rail + 16 gap
    }
  } else if (d.marg < 16) {
    d.marg = 16;                        // clear the sliver
  }
  return d;
}

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
/**
 * Give the status the whole bottom row when nothing is sharing it.
 *
 * The reserve exists so a long status cannot run into the situation chip; with
 * an empty right-hand column there is nothing to run into, and holding 81 px
 * back only truncates. F1 sends "8/21 - 6:30 AM" for a scheduled session and
 * it printed as "8/21 -...".
 */
static void setStatusWidth(TileUI& t, const DensitySpec& d, bool rightInUse) {
  const int leftX = TILE_PAD_X + 12;
  const int w = rightInUse ? STATUS_W : (d.tileW - TILE_PAD_X - leftX);
  if (t.cStatusW == (int16_t)w) return;
  t.cStatusW = (int16_t)w;
  lv_obj_set_width(t.status, w);
}

static void setHiddenCached(lv_obj_t* o, bool* cache, bool hide) {
  if (!o) return;                // never panic on a tile that was not built
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
  // The signature edge light, which now marks the leading team's ROW rather
  // than the tile's perimeter.
  //
  // It used to run the full tile height and jump to the RIGHT edge when the
  // home side led. Across a 12 px gutter that put it 6 px from the next tile's
  // left edge, so nine tiles of it read as column rules between cards instead
  // of as "this side is ahead" — the one thing it exists to say. Anchored to
  // the row, at x=0, it points at a team.
  t.edge = lv_obj_create(t.root);
  lv_obj_remove_style_all(t.edge);
  lv_obj_set_size(t.edge, EDGE_W, ((d.tileH - 2 * TILE_PAD_Y - STATUS_H) / 2) - 12);
  lv_obj_set_pos(t.edge, 0, TILE_PAD_Y + 6);
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
    //
    // It is its OWN object rather than a border on the badge, because the badge
    // is hidden the moment a logo replaces it — the mark used to show only on
    // teams whose logo had not loaded, which is exactly backwards.
    t.favRing[i] = lv_obj_create(t.root);
    lv_obj_remove_style_all(t.favRing[i]);
    lv_obj_set_size(t.favRing[i], d.badge + 6, d.badge + 6);
    lv_obj_set_pos(t.favRing[i], TILE_PAD_X - 3, mid - d.badge / 2 - 3);
    lv_obj_set_style_radius(t.favRing[i], 10, 0);
    lv_obj_set_style_bg_opa(t.favRing[i], LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(t.favRing[i], C_INK, 0);
    lv_obj_set_style_border_opa(t.favRing[i], LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(t.favRing[i], 2, 0);
    lv_obj_add_flag(t.favRing[i], LV_OBJ_FLAG_HIDDEN);
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
    t.cLogoKey[i][0] = '\0';

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
  // LV_LABEL_LONG_DOT only ellipsises once the text exceeds the label's
  // HEIGHT; with an unconstrained height it wraps freely first. So "Wyndham
  // Championship" broke onto a second line and printed straight over the
  // leaderboard's first two rows on the real panel. Pinning the height to one
  // line is what actually makes the mode ellipsise.
  lv_obj_set_height(t.fldTitle, (int)lv_font_get_line_height(F_BODY));
  // Three columns across 160 px on a Dense tile, so every pixel is spoken for,
  // and the name is the column that was losing.
  //
  // LV_LABEL_LONG_DOT with a pinned height wraps at a SPACE and only then
  // adds the dots, so a name two pixels too wide does not lose two pixels —
  // it collapses to its first word. "M. Brennan" rendered as "M....", which
  // names nobody. Widening by four pixels does not fix that; it just moves
  // which names fall off the cliff.
  //
  // So on a narrow tile the POSITION column goes instead. Three rows in rank
  // order carry their own position, and the ~26 px it was costing is the
  // difference between a name and an initial. The cost is real and bounded:
  // a tie ("T3") is no longer marked on Dense.
  const bool wideTile = d.tileW >= 220;
  const int posW = wideTile ? 20 : 0;
  const int valW = 54;
  const int nameX = TILE_PAD_X + (posW ? posW + 4 : 0);
  const int nameW = d.tileW - TILE_PAD_X - valW - 6 - nameX;
  for (int i = 0; i < 3; i++) {
    const int y = TILE_PAD_Y + 24 + i * 19;
    // NOT hidden when posW is 0 — the refresh's fieldOnly[] loop clears the
    // hidden flag on every field object, so a flag set here would not survive.
    // The refresh writes an empty string instead.
    t.fldPos[i]  = microLabel(t.root, TILE_PAD_X, y, C_INK3, F_NUM);
    // Athlete names carry accents and lowercase — Raikkonen, Perez, Alcaraz.
    t.fldName[i] = microLabel(t.root, nameX, y, C_INK2, F_BODY);
    lv_obj_set_width(t.fldName[i], nameW);
    lv_label_set_long_mode(t.fldName[i], LV_LABEL_LONG_DOT);
    lv_obj_set_height(t.fldName[i], (int)lv_font_get_line_height(F_BODY));
    t.fldVal[i]  = lv_label_create(t.root);
    lv_obj_set_style_text_font(t.fldVal[i], F_NUM, 0);
    lv_obj_set_style_text_color(t.fldVal[i], C_INK, 0);
    lv_obj_set_style_text_align(t.fldVal[i], LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(t.fldVal[i], valW);
    lv_obj_set_pos(t.fldVal[i], d.tileW - TILE_PAD_X - valW, y);
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

  // The state dot. The single accent, doing the one job it exists for: saying
  // "this is happening now" without spending luminance, which is already fully
  // committed to encoding state. Hidden unless the game is live.
  t.dot = lv_obj_create(t.root);
  lv_obj_remove_style_all(t.dot);
  lv_obj_set_size(t.dot, 6, 6);
  lv_obj_set_pos(t.dot, TILE_PAD_X, d.tileH - TILE_PAD_Y - 10);
  lv_obj_set_style_radius(t.dot, 3, 0);
  lv_obj_set_style_bg_color(t.dot, C_LIVE, 0);
  lv_obj_set_style_bg_opa(t.dot, LV_OPA_COVER, 0);
  lv_obj_add_flag(t.dot, LV_OBJ_FLAG_HIDDEN);
  pulseRegister(t.dot);

  // ── the bottom row: status on the left, ONE right-hand label ─────────────
  //
  // Both columns are now derived from the tile width instead of being fixed,
  // because a Dense tile is 186 px and the fixed numbers were sized for a
  // 248 px one. On the real panel that printed "Bot 8tl2 ON 2 OUT" — the
  // status and the situation chip straight through each other.
  //
  // F_NUM is monospaced at exactly 9.0 px. The longest status the proxy sends
  // is nine glyphs ("3rd 04:21"), so the left column is 81 px on every tile
  // and the right-hand label takes whatever is left after a 12 px gutter:
  // 117 px on Standard, 55 px on Dense. situationText() switches to a
  // five-glyph vocabulary when that is all the room there is.
  const int rowY   = d.tileH - TILE_PAD_Y - 15;
  const int leftX  = TILE_PAD_X + 12;      // past the live dot's gutter
  const int rightX = leftX + STATUS_W + 12;
  const int rightW = d.tileW - TILE_PAD_X - rightX;

  // Indented past the dot's gutter whether the dot is showing or not, so the
  // status line does not shift sideways when a game goes live. Bounded, so it
  // cannot reach the right-hand label however long upstream makes it.
  t.status = microLabel(t.root, leftX, rowY, C_INK2, F_NUM);
  lv_obj_set_width(t.status, STATUS_W);
  lv_label_set_long_mode(t.status, LV_LABEL_LONG_DOT);

  t.bcast  = lv_label_create(t.root);
  lv_obj_set_style_text_font(t.bcast, F_NUM, 0);
  lv_obj_set_style_text_color(t.bcast, C_INK3, 0);
  lv_obj_set_style_text_align(t.bcast, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_width(t.bcast, rightW);
  // Fixed width plus the default LONG_WRAP clips mid-word: "Prime Video" drew
  // as "Prime Vid" with the rest simply gone. An ellipsis at least says that
  // something was cut. Confirmed in desktop scenario 3.
  lv_label_set_long_mode(t.bcast, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(t.bcast, rightX, rowY);
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
  // Amber: the situation is a caution-class fact ("something is at stake
  // RIGHT NOW"), and C_WARN is the only non-team hue with that meaning.
  lv_obj_set_style_text_color(t.sit, C_WARN, 0);
  lv_obj_set_style_text_align(t.sit, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_width(t.sit, rightW);
  lv_label_set_long_mode(t.sit, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(t.sit, rightX, rowY);
  lv_label_set_text(t.sit, "");
  lv_obj_add_flag(t.sit, LV_OBJ_FLAG_HIDDEN);

  memset(t.cAbbr, 0, sizeof t.cAbbr);
  memset(t.cRec, 0, sizeof t.cRec);
  memset(t.cStatus, 0, sizeof t.cStatus);
  memset(t.cBcast, 0, sizeof t.cBcast);
  t.cLogoKey[0][0] = t.cLogoKey[1][0] = '\0';
  t.cFav[0] = t.cFav[1] = false;
  // Must match the objects' real state at build time: badge visible, logo not.
  t.cBadgeVis[0] = t.cBadgeVis[1] = true;
  t.cLogoVis[0] = t.cLogoVis[1] = false;
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
  t.cLead = false;
  // Must match the objects' real state at build time — see the note above.
  t.cSitVis = false;
  t.cBcastVis = true;
  t.cDotVis = false;
  t.cShape = -1;
  t.cScoreInk[0] = t.cScoreInk[1] = 0xFFFFFFFF;
  t.cChip[0] = t.cChip[1] = 0xFFFFFFFFu;
  t.cStatusW = (int16_t)STATUS_W;      // matches the width set above
  t.cUsed = true;
}

/** A 54x32 nav pill. Filled LIGHTER than the bar — that is the affordance
 *  the old bordered-darker boxes got backwards: the touchable thing should
 *  look raised, not recessed. Words, not abbreviations: TBL needed decoding.
 *  Shared with the idle screen via uiNavPill(). */
lv_obj_t* uiNavPill(lv_obj_t* bar, int x, int barH, const char* text, lv_event_cb_t cb) {
  lv_obj_t* b = lv_btn_create(bar);
  lv_obj_set_size(b, 54, 32);
  lv_obj_set_pos(b, x, (barH - 32) / 2);
  lv_obj_set_style_bg_color(b, C_SURF_1, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(b, C_EDGE, 0);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_radius(b, 8, 0);
  lv_obj_set_style_bg_color(b, C_EDGE_HI, LV_STATE_PRESSED);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* l = lv_label_create(b);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, F_MICRO, 0);
  lv_obj_set_style_text_letter_space(l, 1, 0);
  lv_obj_set_style_text_color(l, C_INK2, 0);
  lv_obj_center(l);
  return b;
}

static void onTableBtn(lv_event_t*) {
  uiStandingsOpen(standingsLeague());
}
static void onNewsBtn(lv_event_t*) { uiNewsOpen(); }
static void onSettingsBtn(lv_event_t*) { uiSettingsOpen(); }

static void onPill(lv_event_t*) { s_lastTouchMs = millis(); uiRailToggle(); }
static void onNavTouch() { s_lastTouchMs = millis(); }

static void zoneCApply();

static void buildBar() {
  const int barH = spec().barH;
  auto midY = [barH](const lv_font_t* f) {
    return (barH - (int)lv_font_get_line_height(f)) / 2;
  };
  s_bar = glassPanel(s_board, 0, 0, SCR_W, barH, 0);

  // Zone A — the live organ. The first fixation point on the panel now says
  // the one thing this product exists to say. The count is display-size: at
  // chip size it read as a superscript (caught in the LVGL mockup pass).
  s_zaDot = lv_obj_create(s_bar);
  lv_obj_remove_style_all(s_zaDot);
  lv_obj_set_size(s_zaDot, 9, 9);
  lv_obj_set_pos(s_zaDot, 14, (barH - 9) / 2);
  lv_obj_set_style_radius(s_zaDot, 5, 0);
  lv_obj_set_style_bg_color(s_zaDot, C_LIVE, 0);
  lv_obj_set_style_bg_opa(s_zaDot, LV_OPA_COVER, 0);
  pulseRegister(s_zaDot);
  s_zaLive  = microLabel(s_bar, 30, midY(F_MICRO), C_LIVE_SD, F_MICRO);
  lv_obj_set_style_text_letter_space(s_zaLive, 2, 0);
  lv_label_set_text(s_zaLive, "LIVE");
  s_zaCount = microLabel(s_bar, 72, (barH - 30) / 2, C_LIVE, F_DISPLAY);
  s_zaTotal = microLabel(s_bar, 96, midY(F_MICRO), C_INK3, F_MICRO);
  lv_obj_t* div = lv_obj_create(s_bar);
  lv_obj_remove_style_all(div);
  lv_obj_set_size(div, 1, 20);
  lv_obj_set_pos(div, 134, (barH - 20) / 2);
  lv_obj_set_style_bg_color(div, C_EDGE, 0);
  lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);

  // Zone B — the filter pill. Names the current filter in words and is the
  // rail's PRIMARY opener: a 16 px sliver is an undiscoverable Fitts target,
  // and hidden nav with only a hairline affordance is how drawers fail.
  s_pill = lv_btn_create(s_bar);
  lv_obj_set_size(s_pill, 224, 30);
  lv_obj_set_pos(s_pill, 148, (barH - 30) / 2);
  lv_obj_set_style_bg_color(s_pill, C_SURF_1, 0);
  lv_obj_set_style_bg_opa(s_pill, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s_pill, 0, 0);
  lv_obj_set_style_radius(s_pill, 7, 0);
  lv_obj_set_style_bg_color(s_pill, C_EDGE_HI, LV_STATE_PRESSED);
  lv_obj_add_event_cb(s_pill, onPill, LV_EVENT_CLICKED, nullptr);
  s_pillLbl = lv_label_create(s_pill);
  lv_obj_set_style_text_font(s_pillLbl, F_MICRO, 0);
  lv_obj_set_style_text_letter_space(s_pillLbl, 1, 0);
  lv_obj_set_style_text_color(s_pillLbl, C_INK2, 0);
  lv_label_set_text(s_pillLbl, "");
  lv_obj_center(s_pillLbl);
  s_pillUnder = lv_obj_create(s_bar);
  lv_obj_remove_style_all(s_pillUnder);
  lv_obj_set_size(s_pillUnder, 212, 2);
  lv_obj_set_pos(s_pillUnder, 154, (barH - 30) / 2 + 28);
  lv_obj_set_style_bg_color(s_pillUnder, C_LIVE_SD, 0);
  lv_obj_set_style_bg_opa(s_pillUnder, LV_OPA_COVER, 0);
  lv_obj_add_flag(s_pillUnder, LV_OBJ_FLAG_HIDDEN);

  // Zone C — one slot with a precedence, not three labels fighting for the
  // same pixels. Trouble > delta > clock; the three are mutually exclusive
  // (if the feed is broken, "since you looked" is a lie), which is what
  // retires s_lblStatus and its chip-collision bug by deletion.
  s_zc = lv_obj_create(s_bar);
  lv_obj_remove_style_all(s_zc);
  lv_obj_set_size(s_zc, 150, 24);
  lv_obj_set_pos(s_zc, 460, (barH - 24) / 2);
  lv_obj_set_style_radius(s_zc, 7, 0);
  lv_obj_clear_flag(s_zc, LV_OBJ_FLAG_SCROLLABLE);
  s_zcLbl = lv_label_create(s_zc);
  lv_obj_set_style_text_font(s_zcLbl, F_MICRO, 0);
  lv_obj_set_style_text_letter_space(s_zcLbl, 1, 0);
  lv_obj_set_style_text_color(s_zcLbl, C_INK2, 0);
  lv_label_set_text(s_zcLbl, "");
  lv_obj_align(s_zcLbl, LV_ALIGN_RIGHT_MID, -4, 0);

  uiNavPill(s_bar, 622, barH, "TABLE", [](lv_event_t*){ onNavTouch(); onTableBtn(nullptr); });
  lv_obj_t* news = uiNavPill(s_bar, 622 + 58, barH, "NEWS",
                             [](lv_event_t*){ onNavTouch(); g_newsUnread = false;
                                              if (s_navNewsDot) lv_obj_add_flag(s_navNewsDot, LV_OBJ_FLAG_HIDDEN);
                                              onNewsBtn(nullptr); });
  uiNavPill(s_bar, 622 + 116, barH, "SETUP", [](lv_event_t*){ onNavTouch(); onSettingsBtn(nullptr); });
  s_navNewsDot = lv_obj_create(s_bar);
  lv_obj_remove_style_all(s_navNewsDot);
  lv_obj_set_size(s_navNewsDot, 7, 7);
  lv_obj_set_pos(s_navNewsDot, lv_obj_get_x(news) + 47, (barH - 32) / 2 - 2);
  lv_obj_set_style_radius(s_navNewsDot, 4, 0);
  lv_obj_set_style_bg_color(s_navNewsDot, C_LIVE, 0);
  lv_obj_set_style_bg_opa(s_navNewsDot, LV_OPA_COVER, 0);
  lv_obj_add_flag(s_navNewsDot, LV_OBJ_FLAG_HIDDEN);

  // The signature: the poll heartbeat, along the bar's bottom edge.
  lv_obj_t* hb = lv_obj_create(s_board);
  lv_obj_remove_style_all(hb);
  lv_obj_set_size(hb, 1, 2);
  lv_obj_set_pos(hb, 0, barH - 2);
  lv_obj_set_style_bg_color(hb, C_LIVE_SD, 0);
  lv_obj_set_style_bg_opa(hb, LV_OPA_COVER, 0);
  s_heart[0] = hb;

  // Page dots, relocated past the pill (the chips they used to dodge are gone).
  for (uint8_t i = 0; i < 8; i++) {
    s_dot[i] = lv_obj_create(s_bar);
    lv_obj_remove_style_all(s_dot[i]);
    lv_obj_set_size(s_dot[i], 6, 6);
    lv_obj_set_style_radius(s_dot[i], 3, 0);
    lv_obj_set_style_bg_opa(s_dot[i], LV_OPA_COVER, 0);
    lv_obj_add_flag(s_dot[i], LV_OBJ_FLAG_HIDDEN);
  }
  s_lblPage = nullptr;
  c_zaCount[0] = c_zaTotal[0] = c_pill[0] = '\0';
  // The zone C cache guards a label this function just recreated EMPTY. A
  // surviving cache suppresses the first write after any rebuild — and
  // rebuilds fire from rail toggles, density cycles, settings close, and the
  // auto-density guard on ordinary polls. Review r1, blocker 1.
  s_zcCache[0] = '\x01'; s_zcCache[1] = '\0';

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

int8_t uiBoardTileGame(uint8_t slot) {
  return slot < TILES_PER_PAGE ? s_tile[slot].gameIdx : -1;
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
  // Clamped while the rail is open: the rail is a mode, and paging under it
  // would re-rank the very tiles the overlay is protecting from taps.
  if (uiRailOpen()) return false;
  // FEATURE has exactly one page — the ledger absorbs whatever does not fit,
  // so there is never anything on a second one.
  if (isFeature()) return false;
  const uint8_t per = spec().cols * spec().rows;
  const uint8_t pages = (visibleCount() + per - 1) / per;
  if (pages <= 1) return false;
  const int next = (int)g_page + delta;
  g_page = (uint8_t)((next % pages + pages) % pages);
  uiBoardRefresh();
  return true;
}

/**
 * Where the finger went down. LVGL sends SHORT_CLICKED on release regardless
 * of how far the press travelled, so a swipe across a tile arrives as BOTH a
 * gesture and a tap — the board paged AND opened the game you happened to
 * start the swipe on, and backing out left you on the new page.
 *
 * Comparing press to release is order-independent, which matters: gesture and
 * click are both emitted during release handling and relying on which comes
 * first is not something to build on.
 */
static lv_point_t s_pressPt;
#define TAP_SLOP 18       // px; a deliberate tap does not travel this far

static void onTileEvent(lv_event_t* e) {
  const lv_event_code_t code = lv_event_get_code(e);
  TileUI* t = (TileUI*)lv_event_get_user_data(e);
  if (!t) return;

  if (code == LV_EVENT_PRESSED) {
    lv_indev_get_point(lv_indev_get_act(), &s_pressPt);
    s_lastTouchMs = millis();
    if (s_deltaCount) { s_deltaCount = 0; s_deltaUntil = 0; zoneCApply(); }
    return;
  }

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
    lv_point_t now;
    lv_indev_get_point(lv_indev_get_act(), &now);
    const int dx = now.x - s_pressPt.x, dy = now.y - s_pressPt.y;
    if (dx * dx + dy * dy > TAP_SLOP * TAP_SLOP) return;   // that was a swipe
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
  // Rebuild BEFORE touching any tile if the layout the array was built for is
  // no longer the layout we want. uiInit() does not call back into here, so
  // this cannot recurse.
  if (s_builtDensity != effectiveDensity() || s_builtRail != uiRailOpen()) { uiInit(); }

  const DensitySpec& d = spec();
  const bool feature = isFeature();
  const uint8_t per = d.cols * d.rows;

  buildOrder();

  // In FEATURE the hero takes rank 0 and the tiles take the next live games;
  // everything settled or scheduled falls to the ledger, so there is exactly
  // one page and nothing to swipe between.
  int8_t heroIdx = -1;
  if (feature) {
    for (uint8_t oi = 0; oi < g_gameCount; oi++) {
      const Game& g = g_board[s_order[oi]];
      if (passesFilter(g) && g.state == GS_LIVE) { heroIdx = (int8_t)s_order[oi]; break; }
    }
    if (heroIdx >= 0) uiHeroShow(heroIdx); else uiHeroHide();
    g_page = 0;
  } else {
    uiHeroHide();
    uiLedgerHide();
  }

  const uint8_t pages = feature ? 1
                                : max<uint8_t>(1, (visibleCount() + per - 1) / per);
  if (g_page >= pages) g_page = 0;

  uint8_t seen = 0, slot = 0;
  for (uint8_t oi = 0; oi < g_gameCount && slot < per; oi++) {
    const uint8_t i = s_order[oi];
    const Game& g = g_board[i];
    if (!passesFilter(g)) continue;
    // The hero is not repeated as a tile, and in FEATURE the two tiles are for
    // live games only — a scheduled fixture belongs in the ledger, not in a
    // 248x128 box saying "-" and "-".
    if (feature && ((int8_t)i == heroIdx || g.state != GS_LIVE)) continue;
    if (seen++ < g_page * per) continue;

    TileUI& t = s_tile[slot];
    const bool sameGame = t.gameIdx == (int8_t)i;
    t.gameIdx = (int8_t)i;
    setHiddenCached(t.root, &t.cUsed, false);

    // State ink. This used to be one opacity on the tile root, which faded the
    // frost and the text together and took a final's record and broadcast to
    // 1.73:1 — so a finished game read as a tile that had failed to load. It
    // also pushed every tile through a 63 KB composite buffer.
    // The lead tile: slot 0 of page 0, live, grid layouts only (FEATURE's
    // hero already is the focal point). One rung up the surface ladder — the
    // whole treatment is a bg colour and the SI_HERO ink row.
    const bool lead = !feature && slot == 0 && g_page == 0 && g.state == GS_LIVE;
    const StateInk& si = lead ? kStateInk[SI_HERO] : kStateInk[g.state];
    if (t.cState != g.state || t.cLead != lead) {
      t.cState = g.state;
      t.cLead = lead;
      lv_obj_set_style_bg_color(t.root, si.plate, 0);
      lv_obj_set_style_border_color(t.root, si.edge, 0);
      for (int k = 0; k < 2; k++) {
        lv_obj_set_style_text_color(t.abbr[k], si.ink, 0);
        lv_obj_set_style_text_color(t.rec[k],  si.ink3, 0);
      }
      // Live status reads AS live from across the desk — the family's solved
      // body tint, not a grey. Finals and pre stay in the state ink.
      lv_obj_set_style_text_color(t.status, g.state == GS_LIVE ? C_LIVE_TX : si.ink2, 0);
      lv_obj_set_style_text_color(t.bcast,  si.ink3, 0);
      lv_obj_set_style_text_color(t.fldTitle, si.ink, 0);
      // The score-ink cache stores a team colour or a "use si.ink2" sentinel,
      // and si.ink2 has just moved. Invalidate, or a non-leading side keeps the
      // previous state's ink2 for as long as it stays behind.
      t.cScoreInk[0] = t.cScoreInk[1] = 0xFFFFFFFE;
    }

    // Three models do not have two sides and a score. Show/hide the two
    // shapes rather than building separate tiles: the grid, the frost and the
    // edge light are identical, only the contents differ.
    const bool isField = (g.model == SM_LEADERBOARD || g.model == SM_GRID);
    const bool isSet   = (g.model == SM_SET);
    // NOTE the badges are NOT in this list. This loop runs on every refresh, so
    // including them meant the badge was un-hidden every time — straight back
    // out from under the logo that had just replaced it. The abbreviation then
    // showed through the logo drawn on top of it.
    //
    // Badge and logo visibility belong to exactly one place: the logo block
    // below. Two owners of one flag is not a race here, it is just a bug that
    // repaints forever.
    // GATED on the tile's shape actually changing. lv_obj_clear_flag is not
    // idempotent — it invalidates unconditionally on every call — so running
    // these loops per refresh burned up to 16 invalidation regions per tile
    // per poll for no pixel change, threatening the 64-region ceiling this
    // file is architected around. Review r1, blocker 3.
    if (t.cShape != (int8_t)isField) {
      t.cShape = (int8_t)isField;
      lv_obj_t* const twoSided[] = { t.abbr[0], t.abbr[1],
                                     t.rec[0], t.rec[1], t.score[0], t.score[1] };
      for (lv_obj_t* o : twoSided)
        isField ? lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN) : lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
      lv_obj_t* const fieldOnly[] = { t.fldTitle, t.fldPos[0], t.fldPos[1], t.fldPos[2],
                                      t.fldName[0], t.fldName[1], t.fldName[2],
                                      t.fldVal[0], t.fldVal[1], t.fldVal[2] };
      for (lv_obj_t* o : fieldOnly)
        isField ? lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
    if (isField) {
      for (int k = 0; k < 2; k++) {
        setHiddenCached(t.badge[k], &t.cBadgeVis[k], true);
        setHiddenCached(t.logo[k],  &t.cLogoVis[k],  true);
        setHiddenCached(t.favRing[k], &t.cFav[k], true);
      }
    }

    if (isField) {
      lv_label_set_text(t.fldTitle, g.away.name);
      const FieldSet* F = (g.fieldIdx >= 0 && g.fieldIdx < FLD_POOL) ? &g_fields[g.fieldIdx] : nullptr;
      for (int k = 0; k < 3; k++) {
        const bool on = F && k < F->count;
        // Dropped on narrow tiles so the name gets the width — see buildTile().
        lv_label_set_text(t.fldPos[k], (on && d.tileW >= 220) ? F->rows[k].pos : "");
        lv_label_set_text(t.fldName[k], on ? F->rows[k].name : (k == 0 && !F ? g.home.name : ""));
        lv_label_set_text(t.fldVal[k],  on ? F->rows[k].val : "");
      }
      setTextCached(t.status, t.cStatus, sizeof t.cStatus, g.status);
      setTextCached(t.bcast,  t.cBcast,  sizeof t.cBcast,  g.bcast);
      setStatusWidth(t, d, g.bcast[0] != '\0');
      // State ink already applied above; a leaderboard has no leader side.
      setHiddenCached(t.sit,   &t.cSitVis,   true);
      setHiddenCached(t.bcast, &t.cBcastVis, false);
      setHiddenCached(t.edge, &t.cEdgeVis, true);
      setHiddenCached(t.dot,  &t.cDotVis, g.state != GS_LIVE);
      // The tennis set boxes. They are hidden in the two-sided path below,
      // which this branch never reaches — so a tile that held a tennis match
      // and then became a golf leaderboard kept them, printing set scores
      // straight through the players' names. Same class as the badge/logo bug:
      // a hide that only ran on one of the two paths out of here.
      for (int k = 0; k < 2; k++) lv_obj_add_flag(t.setLbl[k], LV_OBJ_FLAG_HIDDEN);
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
        // The delta ledger counts scores that changed in a slot still holding
        // the SAME game — a page flip reassigns slots and must not count.
        if (sameGame && g.state == GS_LIVE &&
            t.cScore[k] >= 0 && t.cScore[k] != (int32_t)side[k]->score &&
            millis() - s_lastTouchMs > 5000) {
          s_deltaCount++;
          s_deltaUntil = millis() + 60000;
          zoneCApply();
        }
        setNumCached(t.score[k], &t.cScore[k], side[k]->score);
      }
      // The leading score is drawn in the LEADING TEAM'S OWN COLOUR, lifted
      // against this state's fill. That is roughly 900 px of team colour at the
      // point of the tile the eye lands on, which is what the 3 px perimeter
      // strip was reaching for and could never deliver from the edge.
      //
      // The losing side drops to ink-2 — the second half of the state channel.
      // Change-cached: teamInkOn() walks up to 120 contrast evaluations, and
      // this runs for every side of every tile on every refresh.
      const bool leading = (g.state != GS_PRE) &&
                           ((k == 1) == g.leaderHome) && (g.away.score != g.home.score);
      // 0xFFFFFFFF is the "not leading, use this state's ink2" sentinel — ink2
      // is not round-tripped through the cache, because lv_color_t is RGB565
      // here and reconstructing it would quietly shift the colour.
      const uint32_t want = leading ? teamInkOn(side[k]->color, si.fill) : 0xFFFFFFFF;
      if (t.cScoreInk[k] != want) {
        t.cScoreInk[k] = want;
        lv_obj_set_style_text_color(
            t.score[k], want == 0xFFFFFFFF ? si.ink2 : lv_color_hex(want), 0);
      }

      // Followed teams keep a ring — see buildTile().
      const bool fav = sideIsFav(g.league, side[k]->id);
      setHiddenCached(t.favRing[k], &t.cFav[k], !fav);

      // Only tracks the colour now. The badge's FILL and LABEL are owned by
      // the chip block below, which is the single place that knows whether
      // this object is currently a fallback badge or a logo's ground —
      // writing the team colour here unconditionally would repaint over the
      // chip on every refresh, which is the same two-owners bug that made the
      // abbreviation show through the logo.
      if (t.cColor[k] != side[k]->color) {
        t.cColor[k] = side[k]->color;
        t.cChip[k] = 0xFFFFFFFFu;               // force the chip block to reapply
      }

      // Logo when we have one, colour badge otherwise. Change-cached: setting
      // the same source still repaints, and that is what fights the panel DMA.
      // Keyed on the TEAM, not on the descriptor pointer. Slots are a static
      // array, so evicting one and refilling it with another club hands back
      // the same pointer — a pointer-keyed cache would call that "unchanged"
      // and leave one team's logo sitting beside another team's name.
      const lv_img_dsc_t* img = logoGet(g.league, side[k]->abbr);
      char lkey[10];
      snprintf(lkey, sizeof lkey, "%s%s", img ? "" : "-", side[k]->abbr);
      if (strncmp(t.cLogoKey[k], lkey, sizeof t.cLogoKey[k] - 1) != 0) {
        strncpy(t.cLogoKey[k], lkey, sizeof t.cLogoKey[k] - 1);
        t.cLogoKey[k][sizeof t.cLogoKey[k] - 1] = '\0';
        if (img) lv_img_set_src(t.logo[k], img);
      }
      setHiddenCached(t.logo[k], &t.cLogoVis[k], img == nullptr);

      // The badge does double duty. Without a logo it IS the fallback badge.
      // With one, it becomes the CHIP the mark sits on — same rect, same
      // radius, label cleared, logo drawn over it. Reusing it rather than
      // adding a third object is a net reduction, and it retires the
      // badge/logo visibility dance that has already caused one shipped bug.
      //
      // A third of marks solve to no chip and keep the badge hidden, exactly
      // as before. See chipSolve() in theme.h for why this is a solve and not
      // a brightness test.
      const LogoChip chip = img ? logoChip(g.league, side[k]->abbr) : LogoChip{ 0, 0 };
      const uint32_t chipKey = img ? (chip.opa ? chip.color | 0x1000000u : 1u) : 0u;
      if (t.cChip[k] != chipKey) {
        t.cChip[k] = chipKey;
        if (img && chip.opa) {
          lv_obj_set_style_bg_color(t.badge[k], lv_color_hex(chip.color), 0);
          lv_label_set_text(t.badgeLbl[k], "");
        } else if (!img) {
          teamBadgeSet(t.badge[k], side[k]->color);   // restores fill AND ink
          lv_label_set_text(t.badgeLbl[k], side[k]->abbr);
        }
      }
      setHiddenCached(t.badge[k], &t.cBadgeVis[k], img && !chip.opa);
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

    // Compact when the right-hand column cannot hold the full vocabulary —
    // which on Dense it cannot. Derived from the same geometry buildTile used,
    // so the two can never disagree about how much room there is.
    const int rightW = d.tileW - TILE_PAD_X - (TILE_PAD_X + 12 + STATUS_W + 12);
    char sit[20] = "";
    situationText(g, sit, sizeof sit, rightW < SIT_FULL_W);
    setHiddenCached(t.sit,   &t.cSitVis,   !sit[0]);
    setHiddenCached(t.bcast, &t.cBcastVis,  sit[0] != '\0');
    setStatusWidth(t, d, sit[0] != '\0' || g.bcast[0] != '\0');
    if (sit[0]) setTextCached(t.sit, t.cSit, sizeof t.cSit, sit);
    else        t.cSit[0] = '\0';

    // The live accent. Only ever this, only ever live.
    setHiddenCached(t.dot, &t.cDotVis, g.state != GS_LIVE);

    // Edge light: only live games, beside the leading team's ROW.
    const bool edgeOn = (g.state == GS_LIVE);
    setHiddenCached(t.edge, &t.cEdgeVis, !edgeOn);
    if (edgeOn) {
      // teamInkOn against THIS state's fill, not the raw colour: Toronto navy
      // against the tile is 1.11:1, which made the product's signature element
      // invisible on its own flagship example. See theme.cpp.
      const uint32_t c = teamInkOn(g.leaderHome ? g.home.color : g.away.color, si.fill);
      if (t.cEdge != c) { t.cEdge = c; lv_obj_set_style_bg_color(t.edge, lv_color_hex(c), 0); }
      // Vertical, not horizontal: it names a row now. See buildTile().
      const int rowH = (d.tileH - 2 * TILE_PAD_Y - STATUS_H) / 2;
      lv_obj_set_y(t.edge, TILE_PAD_Y + (g.leaderHome ? rowH : 0) + 6);
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
  // Never in FEATURE: the tile strip is anchored to the hero beside it, and
  // floating it down the screen would break that alignment for no gain.
  if (!feature && usedRows && usedRows < d.rows) {
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

  if (feature) {
    // Everything not on the hero or a tile. The ledger is the whole reason the
    // featured layout can cover 62% of the panel where the grid covers 84%.
    lv_obj_add_flag(s_fill, LV_OBJ_FLAG_HIDDEN);
    int8_t used[TILES_PER_PAGE + 1];
    uint8_t nUsed = 0;
    if (heroIdx >= 0) used[nUsed++] = heroIdx;
    for (uint8_t i = 0; i < filled; i++) used[nUsed++] = s_tile[i].gameIdx;
    uiLedgerLayout(uiRailOpen());
    uiLedgerRender(s_order, g_gameCount, used, nUsed, passesFilter);
  } else {
    layoutFiller(d, filled, per);
  }

  // Dots read as "there is more" far better than "1 / 3" does. Hidden while
  // the rail is open: paging is clamped there, and an affordance for a
  // disabled gesture is worse than none.
  const uint8_t shown = (pages > 1 && !uiRailOpen()) ? (pages < 8 ? pages : 8) : 0;
  const int dotsW = shown ? shown * 14 - 8 : 0;
  for (uint8_t p = 0; p < 8; p++) {
    if (p >= shown) { lv_obj_add_flag(s_dot[p], LV_OBJ_FLAG_HIDDEN); continue; }
    lv_obj_clear_flag(s_dot[p], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(s_dot[p], 410 - dotsW / 2 + p * 14, (spec().barH - 6) / 2);
    lv_obj_set_style_bg_color(s_dot[p], p == g_page ? C_INK : C_EDGE_HI, 0);
  }
  uiSetStatus();
  uiRailRefresh();
}

void uiSetClock(const char* hhmm, const char* date) {
  (void)date;                     // the header no longer spends pixels on it
  strncpy(s_clockStr, hhmm, sizeof s_clockStr - 1);
  s_clockStr[sizeof s_clockStr - 1] = '\0';
  zoneCApply();
}

/**
 * Zone C's precedence: trouble > delta > clock. One label, three voices, and
 * the three can never be true at once — a broken feed makes "since you
 * looked" a lie, and a fresh delta is more useful than the time.
 */
static void zoneCApply() {
  char buf[52];
  lv_color_t ink = C_INK2;

  switch (g_net) {
    case NET_NOWIFI:  snprintf(buf, sizeof buf, "NO WI-FI"); ink = C_WARN; break;
    case NET_NOPROXY: snprintf(buf, sizeof buf, "NO PROXY"); ink = C_WARN; break;
    case NET_ERR:     snprintf(buf, sizeof buf, "PROXY UNREACHABLE"); ink = C_WARN; break;
    case NET_STALE: {
      const char* t = lastGoodClock();
      if (t[0]) snprintf(buf, sizeof buf, "AS OF %s", t);
      else      snprintf(buf, sizeof buf, "UPSTREAM STALE");
      ink = C_WARN;
      break;
    }
    case NET_BOOT:    snprintf(buf, sizeof buf, "STARTING"); break;
    default:
      if (s_deltaCount && millis() < s_deltaUntil) {
        snprintf(buf, sizeof buf, "+%u NEW", (unsigned)s_deltaCount);
        ink = C_LIVE;
      } else {
        s_deltaCount = 0;
        snprintf(buf, sizeof buf, "%s", s_clockStr);
      }
      break;
  }
  if (strcmp(s_zcCache, buf) == 0) return;
  strncpy(s_zcCache, buf, sizeof s_zcCache - 1);
  if (s_zcLbl) {
    lv_label_set_text(s_zcLbl, buf);
    lv_obj_set_style_text_color(s_zcLbl, ink, 0);
  }
}

void uiSetStatus() {
  // Zone A: the whole night's count, never the filter's — the filter has the
  // pill, and a zone that changes meaning with the filter cannot be learned
  // peripherally.
  uint8_t live = 0;
  for (uint8_t i = 0; i < g_gameCount; i++) if (g_board[i].state == GS_LIVE) live++;
  char buf[8];
  if (live) {
    snprintf(buf, sizeof buf, "%u", live);
    setTextCached(s_zaCount, c_zaCount, sizeof c_zaCount, buf);
    lv_obj_clear_flag(s_zaCount, LV_OBJ_FLAG_HIDDEN);
    char tot[10];
    snprintf(tot, sizeof tot, "/ %u", (unsigned)g_gameCount);
    // x tracks the count's MEASURED width — a guessed step rendered
    // "LIVE12 /12" with the denominator shaved against a two-digit count.
    const int cw = (int)lv_txt_get_width(buf, (uint32_t)strlen(buf),
                                         F_DISPLAY, 0, LV_TEXT_FLAG_NONE);
    lv_obj_set_x(s_zaTotal, 72 + cw + 8);
    setTextCached(s_zaTotal, c_zaTotal, sizeof c_zaTotal, tot);
    lv_obj_clear_flag(s_zaTotal, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_zaLive, "LIVE");
    lv_obj_set_style_text_color(s_zaLive, C_LIVE_SD, 0);
    lv_obj_set_style_bg_color(s_zaDot, C_LIVE, 0);
  } else {
    lv_obj_add_flag(s_zaCount, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_zaTotal, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_zaLive, g_gameCount ? "NO GAMES LIVE" : "NO GAMES");
    lv_obj_set_style_text_color(s_zaLive, C_INK3, 0);
    lv_obj_set_style_bg_color(s_zaDot, C_EDGE_HI, 0);
    c_zaCount[0] = '\0';
  }

  // Zone B: name the filter, count ITS live games, underline when nonzero.
  uint8_t fLive = 0;
  for (uint8_t i = 0; i < g_gameCount; i++)
    if (g_board[i].state == GS_LIVE && passesFilter(g_board[i])) fLive++;
  char name[12];
  if (g_leagueFilter >= 0 && g_leagueFilter < g_leagueCount) {
    size_t j = 0;
    for (; g_leagues[g_leagueFilter].slug[j] && j < sizeof name - 1; j++)
      name[j] = (char)toupper((unsigned char)g_leagues[g_leagueFilter].slug[j]);
    name[j] = '\0';
  } else {
    strcpy(name, "ALL LEAGUES");
  }
  char pill[30];
  if (fLive) snprintf(pill, sizeof pill, "< %s · %u LIVE", name, fLive);
  else       snprintf(pill, sizeof pill, "< %s", name);
  setTextCached(s_pillLbl, c_pill, sizeof c_pill, pill);
  fLive ? lv_obj_clear_flag(s_pillUnder, LV_OBJ_FLAG_HIDDEN)
        : lv_obj_add_flag(s_pillUnder, LV_OBJ_FLAG_HIDDEN);

  if (s_navNewsDot)
    g_newsUnread ? lv_obj_clear_flag(s_navNewsDot, LV_OBJ_FLAG_HIDDEN)
                 : lv_obj_add_flag(s_navNewsDot, LV_OBJ_FLAG_HIDDEN);
  zoneCApply();
}

void uiHeartbeatSet(uint8_t pct, bool overdue) {
  if (pct > 100) pct = 100;
  const int w = SCR_W * pct / 100;
  if (w == c_heartW && overdue == c_heartWarn) return;
  c_heartW = w;
  c_heartWarn = overdue;
  for (uint8_t i = 0; i < 2; i++) {
    if (!s_heart[i]) continue;
    lv_obj_set_width(s_heart[i], w < 1 ? 1 : w);
    lv_obj_set_style_bg_color(s_heart[i], overdue ? C_WARN : C_LIVE_SD, 0);
  }
}

void uiHeartbeatAdd(lv_obj_t* line) { s_heart[1] = line; }

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
  // Every tile is about to be deleted; drop the pulse timer's pointers first,
  // and the heartbeat lines with them — both hold raw object pointers.
  pulseForget();
  s_heart[0] = nullptr;          // the board's line only; idle's survives
  c_heartW = -1;
  s_builtDensity = effectiveDensity();
  s_builtRail = uiRailOpen();
  s_gridYOff = 0;                 // tiles are about to be rebuilt at their base y
  lv_obj_t* scr = lv_scr_act();
  if (s_board) { lv_obj_del(s_board); s_board = nullptr; }

  s_board = lv_obj_create(scr);
  lv_obj_remove_style_all(s_board);
  lv_obj_set_size(s_board, SCR_W, SCR_H);
  lv_obj_set_pos(s_board, 0, 0);
  // Transparent, so the generated plate shows through. Only one screen root
  // is ever unhidden at a time, so nothing needs an opaque root to cover the
  // screen beneath it — and painting a flat C_PLATE here would hide the very
  // thing plate.cpp exists to draw.
  lv_obj_set_style_bg_color(s_board, C_PLATE, 0);
  lv_obj_set_style_bg_opa(s_board, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(s_board, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_board, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(s_board, onGesture, LV_EVENT_GESTURE, nullptr);

  buildBar();
  // Built in every layout and shown only in FEATURE. They are ~40 objects in
  // PSRAM, which costs nothing to hold; rebuilding them on every layout flip
  // would cost a visible stall.
  uiHeroInit(s_board);
  uiLedgerInit(s_board);

  const uint8_t per = spec().cols * spec().rows;
  for (uint8_t i = 0; i < TILES_PER_PAGE; i++) {
    if (i < per) {
      buildTile(s_tile[i], i);
      lv_obj_add_flag(s_tile[i].root, LV_OBJ_FLAG_CLICKABLE);
      // Without this the tile absorbs the gesture and swiping does nothing.
      lv_obj_clear_flag(s_tile[i].root, LV_OBJ_FLAG_GESTURE_BUBBLE);
      lv_obj_add_event_cb(s_tile[i].root, onTileEvent, LV_EVENT_PRESSED, &s_tile[i]);
      lv_obj_add_event_cb(s_tile[i].root, onTileEvent, LV_EVENT_SHORT_CLICKED, &s_tile[i]);
      lv_obj_add_event_cb(s_tile[i].root, onTileEvent, LV_EVENT_LONG_PRESSED, &s_tile[i]);
      lv_obj_add_event_cb(s_tile[i].root, onGesture, LV_EVENT_GESTURE, nullptr);
    } else {
      s_tile[i].root = nullptr;
      s_tile[i].cUsed = false;
    }
  }
  // LAST, so the rail and its outside-tap overlay sit above every tile.
  uiRailInit(s_board);
  uiShow(s_screen);
}
