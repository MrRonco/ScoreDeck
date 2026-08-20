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

// ── motion (C.6) ───────────────────────────────────────────────────────────
// Event-driven and finite, never continuous. Every animation is skippable:
// a game change, a rebuild or a new value mid-flight sets the end state
// directly. Motion never delays data — the caches store the FINAL value the
// moment it arrives; only the pixels catch up.
static lv_timer_t* s_odo[2];          // M2: the hero-score odometer
static int32_t     s_odoCur[2], s_odoTo[2];
static uint8_t     s_odoStep[2];
static char        c_id[24];          // hero game identity — animations are per-game
static int32_t     s_wpCur = -1;      // M5: displayed win-prob width (cache holds target)
// The ROOT's own visibility. lv_obj_clear_flag invalidates unconditionally,
// so writing it every poll repainted this 508x268 card - 136,144 px - for no
// visual change. setVis() below has always done this correctly for the nine
// children; the root was the one object writing its flag raw.
static bool     s_rootVis;
static int      s_cx = -1, s_cy = -1;   // cached hero origin

static void odoKill(int k) {
  if (s_odo[k]) { lv_timer_del(s_odo[k]); s_odo[k] = nullptr; }
}
static void odoTick(lv_timer_t* tm) {
  const int k = (int)(intptr_t)tm->user_data;
  s_odoStep[k]++;
  int32_t v;
  if (s_odoStep[k] >= 6) {
    v = s_odoTo[k];
    s_odo[k] = nullptr;
    lv_timer_del(tm);
  } else {
    // ease-out: big early steps, settling at the end — 6 frames over 200 ms.
    const float t = s_odoStep[k] / 6.0f;
    const float pr = 1.0f - (1.0f - t) * (1.0f - t);
    v = s_odoCur[k] + (int32_t)((s_odoTo[k] - s_odoCur[k]) * pr + 0.5f);
  }
  char b[8];
  snprintf(b, sizeof b, "%d", (int)v);
  if (s_score[k]) lv_label_set_text(s_score[k], b);
}
static void odoStart(int k, int32_t from, int32_t to) {
  odoKill(k);
  s_odoCur[k] = from;
  s_odoTo[k] = to;
  s_odoStep[k] = 0;
  // 33 ms period: the first frame lands on the tick AFTER the poll's own
  // one-shot invalidation burst, so the two never share a flush.
  s_odo[k] = lv_timer_create(odoTick, 33, (void*)(intptr_t)k);
}

// M8, salvaged hero-only: when the layout flips and the hero APPEARS, it
// steps in over two 80 ms opacity rungs instead of popping. The full-screen
// variant was rejected — four steps of a 384k px screen cannot hold a 60 ms
// cadence (~230 ms of flush each); the hero card alone is alert-class.
static lv_timer_t* s_rev;
static uint8_t     s_revStep;
static void revKill() {
  if (s_rev) { lv_timer_del(s_rev); s_rev = nullptr; }
  if (s_root) lv_obj_set_style_opa(s_root, LV_OPA_COVER, 0);
}
static void revTick(lv_timer_t* tm) {
  // ONE rung, not three. Each rung repaints the whole 508x268 card AND the
  // plate beneath it (bg_opa < COVER fails LV_EVENT_COVER_CHECK, so the
  // background cannot be skipped), which cost ~245 ms of flush spread across
  // three steps on a device whose first rule is that nothing may delay data.
  s_revStep++;
  if (s_root) lv_obj_set_style_opa(s_root, LV_OPA_COVER, 0);
  s_rev = nullptr;
  lv_timer_del(tm);
}

static void wpExec(void* var, int32_t v) {
  (void)var;
  s_wpCur = v;
  const int inner = HERO_W - 2 * HERO_PAD;
  lv_obj_set_size(s_wp[0], inner - v, 4);
  lv_obj_set_size(s_wp[1], v, 4);
  lv_obj_set_pos(s_wp[1], HERO_PAD + inner - v, HERO_H - 10);
}

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

/**
 * Centre the card when it is the only thing on the screen.
 *
 * PLAN item 4.8 costed this — "x = (800-508)/2 = 146, y = 48 + (432-268)/2 =
 * 130. Two cached position writes on one object" — and it did not ship. The
 * origin was two compile-time macros consumed once, with no game-count branch
 * anywhere in the file, so on a quiet evening a 508x268 card sat in the
 * top-left of an 800x480 panel with 118,128 px — 30.8% of the screen — holding
 * no card pixels at all. That reads as content that failed to arrive.
 *
 * Cached, so the common path writes nothing, and applied to the hero and the
 * results row TOGETHER (see the caller): centring one without the other leaves
 * them on different left edges, which is worse than either alone.
 */
void uiHeroCentre(bool noStrip, bool noRow) {
  if (!s_root) return;
  // The two axes answer to different things. Horizontal centring is about the
  // empty TILE STRIP beside the card; vertical centring is about the empty
  // RESULTS ROW beneath it. Centring vertically while the row is populated
  // drops the card straight through it — the hero ends at y=398 and the row
  // starts at 340.
  const int x = noStrip ? (SCR_W - HERO_W) / 2 : HERO_X;
  const int y = (noStrip && noRow) ? (BAR_H + (SCR_H - BAR_H - HERO_H) / 2) : HERO_Y;
  if (s_cx == x && s_cy == y) return;
  s_cx = x; s_cy = y;
  lv_obj_set_pos(s_root, x, y);
}

// ── build ──────────────────────────────────────────────────────────────────
void uiHeroInit(lv_obj_t* parent) {
  s_root = glassPanel(parent, HERO_X, HERO_Y, HERO_W, HERO_H, R_XL);
  lv_obj_set_style_bg_color(s_root, HI().plate, 0);
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
    lv_obj_set_style_radius(s_badge[k], R_LG, 0);
    lv_obj_set_style_text_font(lv_obj_get_child(s_badge[k], 0), F_ABBR, 0);

    // See the grid's note: never set an explicit size on an lv_img in 8.3 —
    // that CLIPS rather than scales. Zoom plus SIZE_MODE_REAL and a 0,0 pivot.
    // Pre-scaled pixels, no transform — see imgscale.cpp.
    s_logo[k] = lv_img_create(s_root);
    lv_obj_set_pos(s_logo[k], HERO_PAD, y);
    lv_obj_add_flag(s_logo[k], LV_OBJ_FLAG_HIDDEN);

    // Created BEFORE the score so it sits behind it in z-order. Centred on
    // where the digits land, and deliberately allowed to run off the ROW —
    // light does not stop at a boundary, and clipping it to the cell is what
    // would make it read as a shape rather than as illumination.
    //
    // Off the row, not off the CARD. That distinction is the phase-15 edit and
    // it is not a reversal of the line above: this file's own logic already
    // protects the specular pair with a row test in bloomComposite for exactly
    // this reason, and the card's 1 px border is the same kind of object. Flat
    // to the card edge, the glow erased that border on 154 of the 232 rows
    // where the two border columns are true border pixels, and left the last
    // interior column at L* 35.59 against a fill of 18.00. It is tapered over
    // the last 24 visible columns and the 16 rows above the footer — both
    // ramps, both measured to cost the digits under 0.4 L*.
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
  // Z-ORDER. Child order is creation order, and the blooms are created inside
  // the per-side loop — so s_bloom[1] was created AFTER s_score[0] and, now
  // that it is OPAQUE, would paint over the leading score's digits. Move both
  // to sit directly above glassPanel's specular pair and below all content.
  //
  // Indices 2 and 3 specifically: glassRelayout() indexes the specular pair
  // POSITIONALLY as children 0 and 1 (theme.cpp), so nothing may be inserted
  // ahead of them. The bloom's opaque disc is capped at 0.80 of its radius so
  // it cannot reach those strips from index 2 either.
  for (int k = 0; k < 2; k++)
    if (s_bloom[k]) lv_obj_move_to_index(s_bloom[k], 2 + k);

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
    lv_obj_set_pos(s_wp[k], 0, HERO_H - 10);
    lv_obj_set_style_radius(s_wp[k], R_XS, 0);
    lv_obj_set_style_bg_opa(s_wp[k], LV_OPA_COVER, 0);
    lv_obj_add_flag(s_wp[k], LV_OBJ_FLAG_HIDDEN);
  }

  // Footer band, re-stacked for the refresh: situation (promoted to the
  // accent — it is the one "happening now" fact), then the last scoring play,
  // then the win-probability bar at the very foot.
  // Two identical accent dots sit on this card 200 px apart and only the top
  // one was ever registered — so one "live" marker pulsed and the other sat
  // flat, measured (57,227,198) against (49,215,181) on the same frame.
  s_footDot = dot(s_root, HERO_PAD, 224);
  pulseRegister(s_footDot);
  // si.ink3 at full opacity, not C_LIVE at text_opa 180. The situation line is
  // a fact about the game, like the clock beside it — and drawn blended it
  // rendered #31AE9C, a colour that is neither the accent nor any declared
  // token. PLAN item 1.9.
  s_foot    = lab(s_root, HERO_PAD + 14, 218, HI().ink3, F_NUM);
  // OUT OF THE GLOW, not under it. Right-aligned to end at HERO_W-144, which
  // clears the bloom's left edge by ~10 px at both card widths (the sprite is
  // centred on the score box, so its left edge tracks HERO_W the same way this
  // does). Under the glow this line measured 2.85:1 median AA-masked — it is
  // si.ink3, the dimmest tier, sitting in the brightest thing on the panel.
  // The alternative was cutting the glow, which put a straight edge under the
  // score; see the note at the top of bloom.cpp. Moving 120 px of tertiary
  // metadata is the cheaper trade by a wide margin.
  s_footR   = lab(s_root, HERO_W - 264, 218, HI().ink3, F_NUM, 120, LV_TEXT_ALIGN_RIGHT);
  // F_BODY, not F_MICRO. This line is upstream prose — "Matthews (24) PP, from
  // Marner", and on any European or Nordic roster "Ødegaard", "Hedström",
  // "Kanté". font_micro13 is ASCII 0x20-0x7E only, so every one of those
  // renders as a hollow box with no warning; theme.h's own rule is that a
  // string which can come from upstream needs F_BODY. ui_game.cpp:140 already
  // draws this exact field in F_BODY, so the two screens disagreed about the
  // same sentence. The letter_space belonged to the 13 px chrome face and is
  // dropped with it — F_BODY is set at its designed tracking.
  s_play    = lab(s_root, HERO_PAD, 240, HI().ink2, F_BODY, HERO_W - 2 * HERO_PAD);

  // Caches must match the objects' real state at build time, or the first
  // update sees a match and skips the write it was meant to make cheap.
  // Labels were just recreated: any in-flight animation now points at dead
  // objects. Kill first, reset the motion state with the caches.
  odoKill(0); odoKill(1);
  if (s_rev) { lv_timer_del(s_rev); s_rev = nullptr; }   // old root is gone
  lv_anim_del(s_wp, nullptr);
  s_wpCur = -1;
  c_id[0] = '\0';
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
  s_rootVis = false;          // cache tracks the flag this line just set
}

lv_obj_t* uiHeroRoot() { return s_root; }
int8_t    uiHeroGameIdx() { return s_gameIdx; }
void      uiHeroHide() {
  if (s_root && s_rootVis) { s_rootVis = false; lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN); }
  s_gameIdx = -1;
  odoKill(0); odoKill(1);
  revKill();
  lv_anim_del(s_wp, nullptr);
}

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
  // Reads the CACHE, not the flag: the flag write below is gated now, so the
  // cache is the authority on whether this call is the card appearing.
  const bool appearing = !s_rootVis;

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
      odoKill(k);
      if (c_score[k] != -2) { c_score[k] = -2; lv_label_set_text(s_score[k], "-"); }
    } else if (c_score[k] != (int32_t)side[k]->score) {
      // M2: the moment the product exists for gets a moment — a 200 ms
      // odometer roll, only for a same-game live increase. Everything else
      // (new game, correction downward, final) writes directly.
      const int32_t from = c_score[k];
      c_score[k] = side[k]->score;
      const bool sameG = (strncmp(c_id, g.id, sizeof c_id - 1) == 0);
      if (sameG && g.state == GS_LIVE && from >= 0 &&
          (int32_t)side[k]->score > from) {
        odoStart(k, from, (int32_t)side[k]->score);
      } else {
        odoKill(k);
        snprintf(buf, sizeof buf, "%u", side[k]->score);
        lv_label_set_text(s_score[k], buf);
      }
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
    //
    // The tiles drop the trailer to ink3. The hero CANNOT, and this is the one
    // place the two surfaces are allowed to disagree, because the hero is the
    // only cell with a glow behind the digits. Measured on --scenario 10 and
    // --scenario 3, ink3 against its AA-masked local background: 3.15:1 and
    // 3.16:1 worst, with 470 of 767 and 1,017 of 2,332 glyph pixels under the
    // 4.5:1 floor. The same pixels in si.ink2 measure 5.00:1 and 5.02:1 worst,
    // none below floor. theme.h's own table already calls ink2 "trailing
    // score"; the hero was the file disagreeing with it.
    //
    // Nothing is lost by it. The hierarchy on this card is carried by HUE —
    // the leader is in its team's colour and the trailer is neutral — so the
    // leader/trailer distinction survives, and a tie has no leader to confuse
    // it with. The row stencil in bloomComposite is the primary fix for the
    // footer; it cannot help here, because the glow is centred on the LEADING
    // row and the trailing digits sit ~84 px from that centre in whichever
    // direction the lead runs.
    //
    // `leading` already requires the scores to differ, so the tie sentinel and
    // the trailer sentinel now name the same colour and one of the two goes.
    const uint32_t want = leading ? teamInkFor(side[k]->color, si.fill)
                                  : 0xFFFFFFFFu;
    if (c_scoreInk[k] != want) {
      c_scoreInk[k] = want;
      lv_obj_set_style_text_color(s_score[k],
          want == 0xFFFFFFFFu ? si.ink2 : lv_color_hex(want), 0);
    }

    // The signature. Only the leading side, only while the game is live —
    // light means "this is happening", so a finished game keeps its colour in
    // the digits and loses the glow.
    const uint32_t glow = (leading && g.state == GS_LIVE)
                          ? teamInkFor(side[k]->color, si.fill) : 0xFFFFFFFFu;
    if (c_bloom[k] != glow) {
      c_bloom[k] = glow;
      if (glow == 0xFFFFFFFFu) bloomSet(s_bloom[k], 0, 0);          // hide
      // Pre-composited against the surface it is actually drawn on, so LVGL
      // copies opaque pixels instead of blending — see bloomComposite().
      else                     bloomComposite(s_bloom[k], glow, si.fill);
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
    const uint32_t c = teamInkFor(g.leaderHome ? g.home.color : g.away.color, si.fill);
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
    const int inner = HERO_W - 2 * HERO_PAD;   // the hero's own inset, not 18
    const int hw = inner * g.winProbHome / 100;
    if (c_wpW != hw) {
      const bool first = (c_wpW == -1);
      c_wpW = hw;                 // the cache holds the TARGET immediately
      lv_obj_set_pos(s_wp[0], HERO_PAD, HERO_H - 10);
      lv_obj_set_style_bg_color(s_wp[0], lv_color_hex(teamInkFor(g.away.color, si.fill)), 0);
      lv_obj_set_style_bg_color(s_wp[1], lv_color_hex(teamInkFor(g.home.color, si.fill)), 0);
      lv_anim_del(s_wp, nullptr);
      if (first || s_wpCur < 0) {
        wpExec(nullptr, hw);      // first appearance snaps — nothing to move from
      } else {
        // M5: the one value that changes continuously finally LOOKS like it
        // does. ~2.4k px/frame for 300 ms, driven through s_wpCur so the
        // pixels and the cache can never disagree at rest.
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_wp);
        lv_anim_set_values(&a, s_wpCur, hw);
        lv_anim_set_time(&a, 300);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
        lv_anim_set_exec_cb(&a, wpExec);
        lv_anim_start(&a);
      }
      for (int k = 0; k < 2; k++) lv_obj_clear_flag(s_wp[k], LV_OBJ_FLAG_HIDDEN);
    }
  }

  char sit[20] = "";
  situationText(g, sit, sizeof sit);
  setVis(s_footDot, &c_footDotVis, !sit[0]);
  setText(s_foot, c_foot, sizeof c_foot, sit);
  setText(s_footR, c_footR, sizeof c_footR, g.bcast);
  setText(s_play, c_play, sizeof c_play, g.lastPlay);

  // Identity captured LAST — the score branch above compares against the
  // PREVIOUS occupant to decide whether a change is a same-game event
  // (animate) or a new tenant (write directly).
  strncpy(c_id, g.id, sizeof c_id - 1);
  c_id[sizeof c_id - 1] = '\0';

  if (!s_rootVis) { s_rootVis = true; lv_obj_clear_flag(s_root, LV_OBJ_FLAG_HIDDEN); }
  if (appearing) {
    revKill();
    lv_obj_set_style_opa(s_root, 150, 0);   // one visible rung, then COVER
    s_revStep = 0;
    s_rev = lv_timer_create(revTick, 80, nullptr);
  }
}
