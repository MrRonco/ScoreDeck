// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
// ui_hero.cpp — the featured game cell.
//
// WHY THIS EXISTS. The nine-up grid gives every game the same 248x128 box, and
// the measurement said what that costs: 84.4% of the panel covered in cards,
// 73.7% of its pixels one mid-grey, and no focal point anywhere. Three of nine
// tiles routinely burned 33% of the screen to say "-" and "-".
//
// Equal weight is not neutrality when the games are not equal. A one-score
// third period involving a team you follow is not the same object as a fixture
// that starts in four hours, and drawing them identically is inaccurate rather
// than fair.
//
// So: one game gets a 508x268 cell, and the thing it spends that space on is
// the LEADING TEAM'S SCORE AT 72 PX IN THAT TEAM'S OWN COLOUR — roughly 2,400
// px of team colour at the point of the screen the eye lands on. That is what
// the 3 px perimeter strip was reaching for and could never deliver from the
// edge of a tile.
#include "ui.h"
#include "theme.h"
#include "../config.h"
#include "../core/state.h"
#include "../net/logos.h"

// Two geometries, one anatomy. Closed rail: the shipped 508 at x=16. Open
// rail: 430 at x=156 — the audit verified the internals fit (the hero digits
// are Archivo Condensed at 31.4-34.5 px/glyph, so even a 3-digit score is
// ~104 px, not the 130 the spec's F_NUM-based arithmetic feared). Everything
// x-positioned from the RIGHT is derived from W so both widths share code.
#define HERO_X   (uiRailOpen() ? 156 : 16)
#define HERO_W   (uiRailOpen() ? 430 : 508)
#define HERO_Y   60
#define HERO_H  268
#define HERO_ROW_H 84          // one side's band, badge included
#define HERO_PAD   24

static lv_obj_t* s_root;
static lv_obj_t* s_league;     // "NHL  ·  SPORTSNET"
static lv_obj_t* s_dot;
static lv_obj_t* s_status;
static lv_obj_t* s_edge;       // leading-row marker
static lv_obj_t* s_badge[2];
static lv_obj_t* s_logo[2];
static lv_obj_t* s_name[2];
static lv_obj_t* s_sub[2];
static lv_obj_t* s_score[2];
static lv_obj_t* s_bloom[2];
static lv_obj_t* s_footDot;
static lv_obj_t* s_foot;
static lv_obj_t* s_footR;
static lv_obj_t* s_play;      // the last scoring play
static lv_obj_t* s_wp[2];
static int8_t    s_gameIdx = -1;

// Change cache. Same discipline as the grid: LVGL repaints on any write, even
// one that changes nothing, and unchanged writes fight the panel DMA.
static char     c_league[24], c_status[16], c_foot[24], c_footR[13], c_play[44];
static char     c_name[2][24], c_sub[2][10], c_logoKey[2][10];
static int32_t  c_score[2];
static uint32_t c_color[2], c_scoreInk[2], c_edgeC, c_bloom[2];
static int      c_nameW[2];
static bool     c_logoVis[2], c_badgeVis[2], c_dotVis, c_footDotVis, c_edgeVis;
static int      c_edgeY;
static int      c_wpW = -1;

static const StateInk& HI() { return kStateInk[SI_HERO]; }

// ── helpers ────────────────────────────────────────────────────────────────
static lv_obj_t* lab(lv_obj_t* p, int x, int y, lv_color_t c, const lv_font_t* f,
                     int w = 0, lv_text_align_t al = LV_TEXT_ALIGN_LEFT, int track = 0) {
  lv_obj_t* l = lv_label_create(p);
  lv_obj_set_pos(l, x, y);
  lv_obj_set_style_text_color(l, c, 0);
  lv_obj_set_style_text_font(l, f, 0);
  if (track) lv_obj_set_style_text_letter_space(l, track, 0);
  if (w) {
    lv_obj_set_width(l, w);
    lv_obj_set_style_text_align(l, al, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  }
  lv_label_set_text(l, "");
  return l;
}

static lv_obj_t* hairline(lv_obj_t* p, int x, int y, int w) {
  lv_obj_t* o = lv_obj_create(p);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, 1);
  lv_obj_set_style_bg_color(o, C_LINE, 0);
  lv_obj_set_style_bg_opa(o, OPA_HAIR, 0);
  return o;
}

static lv_obj_t* dot(lv_obj_t* p, int x, int y) {
  lv_obj_t* o = lv_obj_create(p);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, 6, 6);
  lv_obj_set_style_radius(o, 3, 0);
  lv_obj_set_style_bg_color(o, C_LIVE, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  return o;
}

/** F_DISPLAY has no lowercase — see theme.h. Team names arrive mixed-case
 *  ("Maple Leafs"), so they are folded here rather than at a smaller face. */
static void upper(const char* in, char* out, size_t n) {
  size_t i = 0;
  for (; in[i] && i < n - 1; i++) out[i] = (char)toupper((unsigned char)in[i]);
  out[i] = '\0';
}

static void onTap(lv_event_t*) {
  if (s_gameIdx >= 0 && s_gameIdx < g_gameCount) uiGameOpen(g_board[s_gameIdx]);
}

// ── build ──────────────────────────────────────────────────────────────────
void uiHeroInit(lv_obj_t* parent) {
  s_root = glassPanel(parent, HERO_X, HERO_Y, HERO_W, HERO_H, 16);
  lv_obj_set_style_bg_color(s_root, HI().plate, 0);
  lv_obj_set_style_border_color(s_root, HI().edge, 0);
  lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(s_root, onTap, LV_EVENT_SHORT_CLICKED, nullptr);
  // Or the hero swallows the swipe, exactly as the tiles did.
  lv_obj_clear_flag(s_root, LV_OBJ_FLAG_GESTURE_BUBBLE);


  // The dot sits with the string it qualifies — same idiom as the tiles and
  // zone A. Floating it mid-card made it read as a stray pixel that drifted
  // with the status string's length.
  s_dot    = dot(s_root, HERO_PAD, 23);
  s_league = lab(s_root, HERO_PAD + 14, 18, HI().ink3, F_MICRO, 200, LV_TEXT_ALIGN_LEFT, 1);
  pulseRegister(s_dot);
  s_status = lab(s_root, HERO_W - 164, 14, HI().ink, F_NUM, 140, LV_TEXT_ALIGN_RIGHT);
  hairline(s_root, HERO_PAD - 4, 46, HERO_W - 204);

  // The leading-row marker. Vertical, at x=0, aligned to whichever side is
  // ahead — it names a team, not a tile perimeter.
  s_edge = lv_obj_create(s_root);
  lv_obj_remove_style_all(s_edge);
  lv_obj_set_size(s_edge, 4, 52);
  lv_obj_set_style_radius(s_edge, 2, 0);
  lv_obj_set_style_bg_opa(s_edge, LV_OPA_COVER, 0);
  lv_obj_add_flag(s_edge, LV_OBJ_FLAG_HIDDEN);

  for (int k = 0; k < 2; k++) {
    const int y = 66 + k * HERO_ROW_H;

    s_badge[k] = teamBadge(s_root, "", 0x334455, 52);
    lv_obj_set_pos(s_badge[k], HERO_PAD, y);
    lv_obj_set_style_radius(s_badge[k], 12, 0);
    lv_obj_set_style_text_font(lv_obj_get_child(s_badge[k], 0), F_ABBR, 0);

    // See the grid's note: never set an explicit size on an lv_img in 8.3 —
    // that CLIPS rather than scales. Zoom plus SIZE_MODE_REAL and a 0,0 pivot.
    // Pre-scaled pixels, no transform — see imgscale.cpp.
    s_logo[k] = lv_img_create(s_root);
    lv_obj_set_pos(s_logo[k], HERO_PAD, y);
    lv_obj_add_flag(s_logo[k], LV_OBJ_FLAG_HIDDEN);

    // Created BEFORE the score so it sits behind it in z-order. Centred on
    // where the digits land, and deliberately allowed to run off the row —
    // light does not stop at a boundary, and clipping it to the cell is what
    // would make it read as a shape rather than as illumination.
    // Centred on where the DIGITS actually land, MEASURED rather than derived.
    // Two passes of reasoning from the label box were both wrong: F_HERO's
    // 72 px glyphs do not sit where the line box suggests, and the label is
    // right-aligned so the digits are not where its origin is. Rendering the
    // hero and finding the glyph's bounding box put its centre at child
    // (466, y+18) — 46 px right and 12 px down from the second guess.
    s_bloom[k] = bloomCreate(s_root, 220, 220);
    if (s_bloom[k]) lv_obj_set_pos(s_bloom[k], (HERO_W - 42) - 110, (y + 26) - 110);

    s_name[k] = lab(s_root, 92, y, HI().ink, F_DISPLAY, HERO_W - 268);
    s_sub[k]  = lab(s_root, 92, y + 34, HI().ink3, F_NUM, HERO_W - 268);
    s_score[k] = lab(s_root, HERO_W - 168, y, HI().ink2, F_HERO, 144, LV_TEXT_ALIGN_RIGHT);
  }
  // At the optical midpoint between the rows' ink, ending 16 px before the
  // score column so the digits never draw across it.
  hairline(s_root, HERO_PAD - 4, 131, HERO_W - 204);

  // Win probability, along the foot of the cell.
  //
  // Game::winProbHome has been parsed into every game since the wire contract
  // was written and drawn in exactly one place — the game-detail screen you
  // have to tap through to reach. It never appeared on the board at all.
  //
  // It earns its 2,032 px because unlike the score it moves CONTINUOUSLY: it
  // is the only thing on a scoreboard that changes between events, which is
  // most of the time you are looking at it.
  for (int k = 0; k < 2; k++) {
    s_wp[k] = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_wp[k]);
    lv_obj_set_size(s_wp[k], 1, 4);
    lv_obj_set_pos(s_wp[k], 0, HERO_H - 6);
    lv_obj_set_style_bg_opa(s_wp[k], LV_OPA_COVER, 0);
    lv_obj_add_flag(s_wp[k], LV_OBJ_FLAG_HIDDEN);
  }

  // Footer band, re-stacked for the refresh: situation (promoted to the
  // accent — it is the one "happening now" fact), then the last scoring play,
  // then the win-probability bar at the very foot.
  s_footDot = dot(s_root, HERO_PAD, 224);
  s_foot    = lab(s_root, HERO_PAD + 14, 218, C_LIVE, F_NUM, 180);
  s_footR   = lab(s_root, HERO_W - 164, 218, HI().ink3, F_NUM, 140, LV_TEXT_ALIGN_RIGHT);
  s_play    = lab(s_root, HERO_PAD, 240, HI().ink2, F_MICRO, HERO_W - 2 * HERO_PAD);
  lv_obj_set_style_text_letter_space(s_play, 1, 0);

  // Caches must match the objects' real state at build time, or the first
  // update sees a match and skips the write it was meant to make cheap.
  memset(c_league, 0, sizeof c_league);
  memset(c_status, 0, sizeof c_status);
  memset(c_foot, 0, sizeof c_foot);
  memset(c_play, 0, sizeof c_play);
  memset(c_footR, 0, sizeof c_footR);
  memset(c_name, 0, sizeof c_name);
  memset(c_sub, 0, sizeof c_sub);
  memset(c_logoKey, 0, sizeof c_logoKey);
  c_score[0] = c_score[1] = -1;
  c_color[0] = c_color[1] = 0xFFFFFFFF;
  c_scoreInk[0] = c_scoreInk[1] = 0xFFFFFFFF;
  c_edgeC = 0xFFFFFFFF;
  c_bloom[0] = c_bloom[1] = 0xFFFFFFFF;
  c_nameW[0] = c_nameW[1] = -1;
  c_edgeY = -1;
  c_logoVis[0] = c_logoVis[1] = false;
  c_badgeVis[0] = c_badgeVis[1] = true;
  c_dotVis = c_footDotVis = c_edgeVis = false;

  lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t* uiHeroRoot() { return s_root; }
int8_t    uiHeroGameIdx() { return s_gameIdx; }
void      uiHeroHide() { if (s_root) lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN); s_gameIdx = -1; }

// ── update ─────────────────────────────────────────────────────────────────
static void setText(lv_obj_t* o, char* cache, size_t cap, const char* v) {
  if (strncmp(cache, v, cap - 1) == 0) return;
  strncpy(cache, v, cap - 1);
  cache[cap - 1] = '\0';
  lv_label_set_text(o, cache);
}
static void setVis(lv_obj_t* o, bool* cache, bool hide) {
  if (!o || *cache == !hide) return;
  *cache = !hide;
  hide ? lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN) : lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
}

void uiHeroShow(int8_t gameIdx) {
  if (!s_root || gameIdx < 0 || gameIdx >= g_gameCount) { uiHeroHide(); return; }
  s_gameIdx = gameIdx;
  const Game& g = g_board[gameIdx];
  const StateInk& si = HI();

  // League only. This carried "NHL  ·  SPORTSNET" and the footer carried the
  // broadcast too, so the hero printed the channel twice — once in each corner.
  char buf[40];
  upper(g.league, buf, sizeof buf);
  setText(s_league, c_league, sizeof c_league, buf);
  setText(s_status, c_status, sizeof c_status, g.status);
  setVis(s_dot, &c_dotVis, g.state != GS_LIVE);

  const Side* side[2] = { &g.away, &g.home };
  for (int k = 0; k < 2; k++) {
    char up[24];
    upper(side[k]->name[0] ? side[k]->name : side[k]->abbr, up, sizeof up);
    setText(s_name[k], c_name[k], sizeof c_name[k], up);
    setText(s_sub[k], c_sub[k], sizeof c_sub[k], side[k]->rec);

    if (g.state == GS_PRE) {
      if (c_score[k] != -2) { c_score[k] = -2; lv_label_set_text(s_score[k], "-"); }
    } else if (c_score[k] != (int32_t)side[k]->score) {
      c_score[k] = side[k]->score;
      snprintf(buf, sizeof buf, "%u", side[k]->score);
      lv_label_set_text(s_score[k], buf);
    }

    // The name takes every pixel the score's ACTUAL digits leave free —
    // measured, not reserved. A fixed reservation sized for "999" cut
    // "MAPLE LEAFS" to "MAPLE..." at 430 wide beside a one-digit score; the
    // audit's rule is to gate on the rendered width, so a long name only
    // shortens in the case that actually collides.
    {
      char st[8];
      snprintf(st, sizeof st, "%u", (unsigned)side[k]->score);
      const char* shown = (g.state == GS_PRE) ? "-" : st;
      const int sw = (int)lv_txt_get_width(shown, (uint32_t)strlen(shown),
                                           F_HERO, 0, LV_TEXT_FLAG_NONE);
      int nameW = (HERO_W - HERO_PAD - sw - 14) - 92;
      if (nameW < 140) nameW = 140;
      if (c_nameW[k] != nameW) {
        c_nameW[k] = nameW;
        lv_obj_set_width(s_name[k], nameW);
        lv_obj_set_width(s_sub[k], nameW);
      }
    }

    const bool leading = (g.state != GS_PRE) &&
                         ((k == 1) == g.leaderHome) && (g.away.score != g.home.score);
    // 0xFFFFFFFF = "not leading, use si.ink2" — see the grid for why ink2 is
    // never round-tripped through the cache.
    // Same emphasis rule as the tiles: the leader lifts to 5.5:1, the
    // trailer drops to ink3, ties stay ink2. The hero only ever shows live.
    uint32_t want;
    if (g.away.score == g.home.score)
      want = 0xFFFFFFFFu;
    else
      want = leading ? teamInkOn(side[k]->color, si.fill, 6.5f) : 0xFFFFFFFEu;
    if (c_scoreInk[k] != want) {
      c_scoreInk[k] = want;
      lv_obj_set_style_text_color(s_score[k],
          want == 0xFFFFFFFFu ? si.ink2 :
          want == 0xFFFFFFFEu ? si.ink3 : lv_color_hex(want), 0);
    }

    // The signature. Only the leading side, only while the game is live —
    // light means "this is happening", so a finished game keeps its colour in
    // the digits and loses the glow.
    const uint32_t glow = (leading && g.state == GS_LIVE)
                          ? teamInkOn(side[k]->color, si.fill) : 0xFFFFFFFFu;
    if (c_bloom[k] != glow) {
      c_bloom[k] = glow;
      bloomSet(s_bloom[k], glow == 0xFFFFFFFFu ? 0 : glow,
               glow == 0xFFFFFFFFu ? 0 : 200);
    }

    if (c_color[k] != side[k]->color) {
      c_color[k] = side[k]->color;
      teamBadgeSet(s_badge[k], side[k]->color);
      lv_label_set_text(lv_obj_get_child(s_badge[k], 0), side[k]->abbr);
    }

    // Keyed on the TEAM, not the descriptor pointer: logo slots are a static
    // array, so evicting one and refilling it hands back the same pointer.
    const lv_img_dsc_t* img = logoGetScaled(g.league, side[k]->abbr, 52);
    char lkey[10];
    snprintf(lkey, sizeof lkey, "%s%s", img ? "" : "-", side[k]->abbr);
    if (strncmp(c_logoKey[k], lkey, sizeof c_logoKey[k] - 1) != 0) {
      strncpy(c_logoKey[k], lkey, sizeof c_logoKey[k] - 1);
      c_logoKey[k][sizeof c_logoKey[k] - 1] = '\0';
      if (img) lv_img_set_src(s_logo[k], img);
    }
    setVis(s_logo[k],  &c_logoVis[k],  img == nullptr);
    setVis(s_badge[k], &c_badgeVis[k], img != nullptr);
  }

  const bool edgeOn = (g.state == GS_LIVE) && (g.away.score != g.home.score);
  setVis(s_edge, &c_edgeVis, !edgeOn);
  if (edgeOn) {
    const uint32_t c = teamInkOn(g.leaderHome ? g.home.color : g.away.color, si.fill);
    if (c_edgeC != c) { c_edgeC = c; lv_obj_set_style_bg_color(s_edge, lv_color_hex(c), 0); }
    const int y = 66 + (g.leaderHome ? HERO_ROW_H : 0);
    if (c_edgeY != y) { c_edgeY = y; lv_obj_set_pos(s_edge, 0, y); }
  }

  // 255 is the wire's "unavailable"; anything over 100 is nonsense.
  const bool wpOn = (g.state == GS_LIVE) && (g.winProbHome <= 100);
  if (!wpOn) {
    if (c_wpW != -1) { c_wpW = -1;
      for (int k = 0; k < 2; k++) lv_obj_add_flag(s_wp[k], LV_OBJ_FLAG_HIDDEN); }
  } else {
    // Inset past the card's corner radius — at x=0 the bar's square end pokes
    // out of the rounded corner and reads as a rendering fault.
    const int inner = HERO_W - 2 * 18;
    const int hw = inner * g.winProbHome / 100;
    if (c_wpW != hw) {
      c_wpW = hw;
      // Away on the left, home on the right, meeting where the probability
      // sits. Two rects and one width write per change.
      lv_obj_set_size(s_wp[0], inner - hw, 5);
      lv_obj_set_pos(s_wp[0], 18, HERO_H - 10);
      lv_obj_set_size(s_wp[1], hw, 5);
      lv_obj_set_pos(s_wp[1], 18 + inner - hw, HERO_H - 10);
      lv_obj_set_style_bg_color(s_wp[0], lv_color_hex(teamInkOn(g.away.color, si.fill)), 0);
      lv_obj_set_style_bg_color(s_wp[1], lv_color_hex(teamInkOn(g.home.color, si.fill)), 0);
      for (int k = 0; k < 2; k++) lv_obj_clear_flag(s_wp[k], LV_OBJ_FLAG_HIDDEN);
    }
  }

  char sit[20] = "";
  situationText(g, sit, sizeof sit);
  setVis(s_footDot, &c_footDotVis, !sit[0]);
  setText(s_foot, c_foot, sizeof c_foot, sit);
  setText(s_footR, c_footR, sizeof c_footR, g.bcast);
  setText(s_play, c_play, sizeof c_play, g.lastPlay);

  lv_obj_clear_flag(s_root, LV_OBJ_FLAG_HIDDEN);
}
