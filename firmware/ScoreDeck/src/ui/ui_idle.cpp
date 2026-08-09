// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
// ui_idle.cpp — what the panel shows when nothing is live, which is most of the
// day. A Tuesday morning board of dimmed finals is a sad object; this turns the
// screen into a countdown instead. UI.md §7.
//
// Also the cheapest screen in the product: a clock, a countdown and two lists,
// updating at most once a minute. That matters for the state the device holds
// for twenty hours a day.
#include "ui.h"
#include "theme.h"
#include "../config.h"
#include "../core/state.h"
#include <time.h>

static lv_obj_t* s_root;
static lv_obj_t* s_clock;
static lv_obj_t* s_ampm;
static lv_obj_t* s_date;
static lv_obj_t* s_summary;
static lv_obj_t* s_hdrStatus;

static lv_obj_t* s_nextEdge;
static lv_obj_t* s_nextAway;
static lv_obj_t* s_nextHome;
static lv_obj_t* s_nextBadgeA;
static lv_obj_t* s_nextBadgeH;
static lv_obj_t* s_nextLblA;
static lv_obj_t* s_nextLblH;
static lv_obj_t* s_countdown;
static lv_obj_t* s_nextMeta;
static lv_obj_t* s_nextNone;

#define IDLE_ROWS 4
static lv_obj_t* s_todayTime[IDLE_ROWS];
static lv_obj_t* s_todayGame[IDLE_ROWS];
static lv_obj_t* s_todayLg[IDLE_ROWS];
static lv_obj_t* s_finalGame[IDLE_ROWS];
static lv_obj_t* s_finalScore[IDLE_ROWS];

// change caches
static char s_cClock[8], s_cDate[24], s_cSummary[64], s_cCountdown[16], s_cMeta[64];
static char s_cNextId[12];

lv_obj_t* uiIdleRoot() { return s_root; }

static lv_obj_t* lbl(lv_obj_t* p, int x, int y, lv_color_t c, const lv_font_t* f,
                     lv_text_align_t align = LV_TEXT_ALIGN_LEFT, int w = 0) {
  lv_obj_t* l = lv_label_create(p);
  lv_obj_set_pos(l, x, y);
  lv_obj_set_style_text_color(l, c, 0);
  lv_obj_set_style_text_font(l, f, 0);
  if (w) {
    lv_obj_set_width(l, w);
    lv_obj_set_style_text_align(l, align, 0);
  }
  lv_label_set_text(l, "");
  return l;
}

static void setCached(lv_obj_t* o, char* cache, size_t cap, const char* v) {
  if (strncmp(cache, v, cap - 1) == 0) return;
  strncpy(cache, v, cap - 1);
  cache[cap - 1] = '\0';
  lv_label_set_text(o, cache);
}

void uiIdleInit(lv_obj_t* parent) {
  s_root = lv_obj_create(parent);
  lv_obj_remove_style_all(s_root);
  lv_obj_set_size(s_root, SCR_W, SCR_H);
  lv_obj_set_style_bg_color(s_root, C_PLATE, 0);
  lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
  lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);

  // ── header strip ─────────────────────────────────────────────────────────
  lv_obj_t* bar = glassPanel(s_root, 0, 0, SCR_W, BAR_H, 0);
  lv_obj_t* brand = lbl(bar, 18, 17, C_INK2, F_MICRO);
  lv_label_set_text(brand, "SCOREDECK");
  s_hdrStatus = lbl(bar, SCR_W - 18 - 320, 17, C_INK3, F_MICRO, LV_TEXT_ALIGN_RIGHT, 320);

  // ── clock block ──────────────────────────────────────────────────────────
  lv_obj_t* clockCard = glassPanel(s_root, 16, 60, 372, 190, 12);
  s_clock   = lbl(clockCard, 24, 22, C_INK, F_SCORE);
  s_ampm    = lbl(clockCard, 24, 96, C_INK2, F_BODY);
  s_date    = lbl(clockCard, 62, 98, C_INK3, F_MICRO);
  s_summary = lbl(clockCard, 24, 148, C_INK3, F_MICRO);

  // ── next up ──────────────────────────────────────────────────────────────
  lv_obj_t* nextCard = glassPanel(s_root, 400, 60, 384, 190, 12);
  s_nextEdge = lv_obj_create(nextCard);
  lv_obj_remove_style_all(s_nextEdge);
  lv_obj_set_size(s_nextEdge, EDGE_W, 188);
  lv_obj_set_pos(s_nextEdge, 0, 0);
  lv_obj_set_style_bg_opa(s_nextEdge, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(s_nextEdge, 2, 0);

  lv_obj_t* nextHdr = lbl(nextCard, 22, 14, C_INK3, F_MICRO);
  lv_label_set_text(nextHdr, "NEXT UP");

  s_nextBadgeA = teamBadge(nextCard, "", 0x5D6D7E, 34);
  lv_obj_set_pos(s_nextBadgeA, 22, 40);
  s_nextLblA = lv_obj_get_child(s_nextBadgeA, 0);
  s_nextAway = lbl(nextCard, 64, 48, C_INK, F_ABBR);

  s_nextHome = lbl(nextCard, 150, 48, C_INK2, F_ABBR);
  s_nextBadgeH = teamBadge(nextCard, "", 0x5D6D7E, 34);
  lv_obj_set_pos(s_nextBadgeH, 210, 40);
  s_nextLblH = lv_obj_get_child(s_nextBadgeH, 0);

  s_countdown = lbl(nextCard, 22, 92, C_INK, F_DISPLAY);   // carries "H"/"M"/"NOW"
  s_nextMeta  = lbl(nextCard, 22, 156, C_INK3, F_MICRO);
  s_nextNone  = lbl(nextCard, 22, 92, C_INK2, F_BODY);
  lv_label_set_text(s_nextNone, "Nothing scheduled");
  lv_obj_add_flag(s_nextNone, LV_OBJ_FLAG_HIDDEN);

  // ── today ────────────────────────────────────────────────────────────────
  lv_obj_t* todayCard = glassPanel(s_root, 16, 262, 372, 202, 12);
  lv_obj_t* th = lbl(todayCard, 20, 14, C_INK3, F_MICRO);
  lv_label_set_text(th, "COMING UP");
  for (int i = 0; i < IDLE_ROWS; i++) {
    const int y = 40 + i * 38;
    s_todayTime[i] = lbl(todayCard, 20, y, C_INK3, F_MICRO);
    s_todayGame[i] = lbl(todayCard, 92, y - 2, C_INK2, F_BODY);
    s_todayLg[i]   = lbl(todayCard, 300, y, C_INK3, F_MICRO, LV_TEXT_ALIGN_RIGHT, 52);
  }

  // ── recent finals (news lands here once it exists) ───────────────────────
  lv_obj_t* finCard = glassPanel(s_root, 400, 262, 384, 202, 12);
  lv_obj_t* fh = lbl(finCard, 20, 14, C_INK3, F_MICRO);
  lv_label_set_text(fh, "LATEST FINALS");
  for (int i = 0; i < IDLE_ROWS; i++) {
    const int y = 40 + i * 38;
    s_finalGame[i]  = lbl(finCard, 20, y - 2, C_INK2, F_BODY);
    s_finalScore[i] = lbl(finCard, 250, y - 2, C_INK, F_BODY, LV_TEXT_ALIGN_RIGHT, 112);
  }

  memset(s_cClock, 0, sizeof s_cClock);
  memset(s_cDate, 0, sizeof s_cDate);
  memset(s_cSummary, 0, sizeof s_cSummary);
  memset(s_cCountdown, 0, sizeof s_cCountdown);
  memset(s_cMeta, 0, sizeof s_cMeta);
  memset(s_cNextId, 0, sizeof s_cNextId);
}

/** The soonest scheduled game, favourites winning ties. */
static const Game* nextGame() {
  const Game* best = nullptr;
  for (uint8_t i = 0; i < g_gameCount; i++) {
    const Game& g = g_board[i];
    if (g.state != GS_PRE) continue;
    if (!best) { best = &g; continue; }
    if (g.isFav != best->isFav) { if (g.isFav) best = &g; continue; }
    if (g.startUtc < best->startUtc) best = &g;
  }
  return best;
}

void uiIdleTick() {
  // Deliberately NOT gated on visibility. uiIdleRefresh() runs before
  // uiShow(SCR_IDLE), so an early return here left the clock, the countdown
  // and the next-up matchup blank on the first frame the screen appeared.
  // Every write below is change-cached, so running while hidden is nearly free.
  if (!s_root) return;

  const time_t now = time(nullptr);
  struct tm lt;
  localtime_r(&now, &lt);

  char buf[64];
  strftime(buf, sizeof buf, "%l:%M", &lt);
  setCached(s_clock, s_cClock, sizeof s_cClock, buf[0] == ' ' ? buf + 1 : buf);
  lv_label_set_text(s_ampm, lt.tm_hour < 12 ? "AM" : "PM");
  strftime(buf, sizeof buf, "%A, %b %e", &lt);
  setCached(s_date, s_cDate, sizeof s_cDate, buf);

  const Game* nx = nextGame();
  lv_obj_t* const matchup[] = { s_nextBadgeA, s_nextBadgeH, s_nextAway, s_nextHome };
  if (!nx) {
    // Empty grey badges read as a rendering fault rather than an empty state.
    for (lv_obj_t* o : matchup) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_countdown, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_nextEdge, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_nextNone, LV_OBJ_FLAG_HIDDEN);
    setCached(s_nextMeta, s_cMeta, sizeof s_cMeta, "");
    s_cNextId[0] = '\0';
    return;
  }
  for (lv_obj_t* o : matchup) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(s_countdown, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(s_nextEdge, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(s_nextNone, LV_OBJ_FLAG_HIDDEN);

  if (strncmp(s_cNextId, nx->id, sizeof s_cNextId - 1) != 0) {
    strncpy(s_cNextId, nx->id, sizeof s_cNextId - 1);
    lv_label_set_text(s_nextAway, nx->away.abbr);
    lv_label_set_text(s_nextHome, nx->home.abbr);
    lv_label_set_text(s_nextLblA, nx->away.abbr);
    lv_label_set_text(s_nextLblH, nx->home.abbr);
    lv_obj_set_style_bg_color(s_nextBadgeA, lv_color_hex(nx->away.color), 0);
    lv_obj_set_style_bg_color(s_nextBadgeH, lv_color_hex(nx->home.color), 0);
    lv_obj_set_style_bg_color(s_nextEdge,
        lv_color_hex(nx->isFav ? nx->home.color : 0x2A3646), 0);
  }

  // Countdown is the hero — it is the reason this screen exists.
  // Units are CAPS because the hero face has no lowercase — see theme.h. As
  // lowercase they rendered as hollow boxes next to the digits.
  const long secs = (long)nx->startUtc - (long)now;
  if (secs <= 0) {
    snprintf(buf, sizeof buf, "NOW");
  } else if (secs < 3600) {
    snprintf(buf, sizeof buf, "%ldM", secs / 60);
  } else if (secs < 86400) {
    snprintf(buf, sizeof buf, "%ldH %ldM", secs / 3600, (secs % 3600) / 60);
  } else {
    snprintf(buf, sizeof buf, "%ldD", secs / 86400);
  }
  setCached(s_countdown, s_cCountdown, sizeof s_cCountdown, buf);

  struct tm st;
  const time_t startT = (time_t)nx->startUtc;
  localtime_r(&startT, &st);
  char tbuf[16];
  strftime(tbuf, sizeof tbuf, "%l:%M %p", &st);
  snprintf(buf, sizeof buf, "%s   %s%s%s", tbuf[0] == ' ' ? tbuf + 1 : tbuf,
           nx->league, nx->bcast[0] ? "   " : "", nx->bcast);
  setCached(s_nextMeta, s_cMeta, sizeof s_cMeta, buf);
}

void uiIdleRefresh() {
  if (!s_root) return;

  uint8_t today = 0, finals = 0;
  for (uint8_t i = 0; i < g_gameCount; i++) {
    if (g_board[i].state == GS_PRE) today++;
    else if (g_board[i].state == GS_FINAL) finals++;
  }

  const Game* nx = nextGame();
  char buf[64];
  if (nx) {
    struct tm st;
    const time_t t = (time_t)nx->startUtc;
    localtime_r(&t, &st);
    char tb[16];
    strftime(tb, sizeof tb, "%l:%M %p", &st);
    snprintf(buf, sizeof buf, "%u scheduled  -  first at %s",
             today, tb[0] == ' ' ? tb + 1 : tb);
  } else {
    snprintf(buf, sizeof buf, "%u games on the board", g_gameCount);
  }
  setCached(s_summary, s_cSummary, sizeof s_cSummary, buf);

  char hs[80];
  switch (g_net) {
    case NET_NOWIFI:  snprintf(hs, sizeof hs, "no wi-fi"); break;
    case NET_NOPROXY: snprintf(hs, sizeof hs, "no proxy configured"); break;
    case NET_ERR:     snprintf(hs, sizeof hs, "%s", g_netDetail[0] ? g_netDetail : "proxy unreachable"); break;
    case NET_STALE:   snprintf(hs, sizeof hs, "stale data"); break;
    default:          snprintf(hs, sizeof hs, "nothing live"); break;
  }
  lv_label_set_text(s_hdrStatus, hs);

  // Coming up: the scheduled games after the hero, in start order.
  uint8_t row = 0;
  bool skippedHero = false;
  for (uint8_t i = 0; i < g_gameCount && row < IDLE_ROWS; i++) {
    const Game& g = g_board[i];
    if (g.state != GS_PRE) continue;
    if (nx && !skippedHero && strcmp(g.id, nx->id) == 0) { skippedHero = true; continue; }
    struct tm st;
    const time_t t = (time_t)g.startUtc;
    localtime_r(&t, &st);
    char tb[16];
    strftime(tb, sizeof tb, "%l:%M", &st);
    lv_label_set_text(s_todayTime[row], tb[0] == ' ' ? tb + 1 : tb);
    snprintf(buf, sizeof buf, "%s  %s  %s", g.away.abbr, g.isFav ? "vs" : "@", g.home.abbr);
    lv_label_set_text(s_todayGame[row], buf);
    lv_label_set_text(s_todayLg[row], g.league);
    row++;
  }
  for (; row < IDLE_ROWS; row++) {
    lv_label_set_text(s_todayTime[row], "");
    lv_label_set_text(s_todayGame[row], "");
    lv_label_set_text(s_todayLg[row], "");
  }

  row = 0;
  for (uint8_t i = 0; i < g_gameCount && row < IDLE_ROWS; i++) {
    const Game& g = g_board[i];
    if (g.state != GS_FINAL) continue;
    snprintf(buf, sizeof buf, "%s  @  %s", g.away.abbr, g.home.abbr);
    lv_label_set_text(s_finalGame[row], buf);
    snprintf(buf, sizeof buf, "%u - %u", g.away.score, g.home.score);
    lv_label_set_text(s_finalScore[row], buf);
    row++;
  }
  for (; row < IDLE_ROWS; row++) {
    lv_label_set_text(s_finalGame[row], "");
    lv_label_set_text(s_finalScore[row], "");
  }

  uiIdleTick();
}
