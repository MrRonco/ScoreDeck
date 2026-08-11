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
#include "../net/logos.h"
#include <time.h>

static lv_obj_t* s_root;
static lv_obj_t* s_clock;
static lv_obj_t* s_ampm;
static lv_obj_t* s_date;
static lv_obj_t* s_summary;
static lv_obj_t* s_hdrStatus;

static lv_obj_t* s_nextCard;
static lv_obj_t* s_nextEdge;
static lv_obj_t* s_nextAway;
static lv_obj_t* s_nextHome;
static lv_obj_t* s_nextBadgeA;
static lv_obj_t* s_nextBadgeH;
// The idle screen is the one on screen for twenty hours a day and it was the
// only one still drawing colour badges — the board had used real logos for
// weeks. Same fallback rule: a logo when one is cached, the badge otherwise.
static lv_obj_t* s_nextLogoA;
static lv_obj_t* s_nextLogoH;
static lv_obj_t* s_nextLblA;
static lv_obj_t* s_nextLblH;
static lv_obj_t* s_countdown;
static lv_obj_t* s_nextMeta;
static lv_obj_t* s_nextNone;

// Three, not four. The ledger rows start at y=384 on a 30 px pitch, so a
// fourth would put its baseline at 489 on a 480 px panel.
#define IDLE_ROWS 3
static lv_obj_t* s_todayTime[IDLE_ROWS];
static lv_obj_t* s_todayGame[IDLE_ROWS];
static lv_obj_t* s_todayLg[IDLE_ROWS];
static lv_obj_t* s_finalGame[IDLE_ROWS];
static lv_obj_t* s_finalScore[IDLE_ROWS];
static lv_obj_t* s_ledHdr[2];
static lv_obj_t* s_ledRule[2];

// change caches
static char s_cClock[8], s_cDate[24], s_cSummary[64], s_cCountdown[16], s_cMeta[64];
static char s_cNextId[12];

// Which game each tappable row is currently showing. The idle screen holds no
// board of its own, so a tap has to resolve back into g_board by index — and
// that index changes on every poll, so it is rewritten in uiIdleRefresh()
// rather than captured once at build time.
static int8_t s_nextIdx = -1;
static int8_t s_rowIdx[IDLE_ROWS];
static int8_t s_finIdx[IDLE_ROWS];

lv_obj_t* uiIdleRoot() { return s_root; }

/** Open a game's detail screen from anywhere on the idle screen.
 *
 *  This is what the NEXT UP card and every ledger row were missing: they were
 *  built as inert text, so the screen the panel spends most of its day showing
 *  was the only one you could not act on. The detail screen already carries
 *  the linescore, scoring plays, team stats and the LINEUP button — a fixture
 *  that has not started yet shows its header and its lineups, which is exactly
 *  the "game day" view that was being asked for.
 */
static void openIdx(int8_t idx) {
  if (idx >= 0 && idx < (int8_t)g_gameCount) uiGameOpen(g_board[idx]);
}
static void onNextCard(lv_event_t*) { openIdx(s_nextIdx); }
static void onTodayRow(lv_event_t* e) {
  openIdx(s_rowIdx[(int)(intptr_t)lv_event_get_user_data(e)]);
}
static void onFinalRow(lv_event_t* e) {
  openIdx(s_finIdx[(int)(intptr_t)lv_event_get_user_data(e)]);
}

static void onIdleSettings(lv_event_t*) { uiSettingsOpen(); }
static void onIdleNews(lv_event_t*)     { uiNewsOpen(); }
static void onIdleTable(lv_event_t*) {
  uiStandingsOpen(standingsLeague());
}

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
  // Transparent, so the generated plate shows through. Only one screen root
  // is ever unhidden at a time, so nothing needs an opaque root to cover the
  // screen beneath it — and painting a flat C_PLATE here would hide the very
  // thing plate.cpp exists to draw.
  lv_obj_set_style_bg_color(s_root, C_PLATE, 0);
  lv_obj_set_style_bg_opa(s_root, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);

  // ── header strip ─────────────────────────────────────────────────────────
  lv_obj_t* bar = glassPanel(s_root, 0, 0, SCR_W, BAR_H, 0);
  // Centred from the face's own line height, not a hardcoded y — BAR_H is 44,
  // 48 or 58 depending on density.
  const int hdrY = (BAR_H - (int)lv_font_get_line_height(F_MICRO)) / 2;
  lv_obj_t* brand = lbl(bar, 18, hdrY, C_INK2, F_MICRO);
  lv_label_set_text(brand, "SCOREDECK");
  // Stops short of the three buttons at the right — they end at SCR_W-18-148.
  s_hdrStatus = lbl(bar, SCR_W - 174 - 260, hdrY, C_INK3, F_MICRO, LV_TEXT_ALIGN_RIGHT, 260);

  // ── clock ────────────────────────────────────────────────────────────────
  //
  // NO CARD. This screen measured 80.5% ONE FLAT COLOUR — a single value
  // across four fifths of the panel, on the screen that is up for most of the
  // day. Four glass cards tiled edge to edge is not a composition, it is a
  // fill; and the least informative screen in the product was the one covering
  // the most of it.
  //
  // The clock is the subject, so it is drawn at 96 px directly on the plate
  // with nothing behind it. Dark ground goes 12.9% -> 48.0% and single-colour
  // dominance 80.5% -> 47.8%, entirely from taking things AWAY.
  s_clock   = lbl(s_root, 44, 68, C_INK, F_CLOCK);
  // Sat at (300,150) — below the digits' baseline and adrift to their right,
  // with the gap between reading as a hole rather than as spacing. A meridiem
  // is a suffix: it belongs on the same baseline, one space away. Measured
  // from the rendered glyphs rather than derived, because F_CLOCK's 96 px
  // digits do not fill their line box.
  s_ampm    = lbl(s_root, 0, 0, C_INK3, F_DISPLAY);
  s_date    = lbl(s_root, 48, 232, C_INK3, F_MICRO);
  lv_obj_set_style_text_letter_space(s_date, 1, 0);
  s_summary = lbl(s_root, 48, 258, C_INK3, F_NUM);

  // ── next up ──────────────────────────────────────────────────────────────
  // The ONLY card on the screen, which is what makes it read as the one thing
  // worth acting on.
  lv_obj_t* nextCard = glassPanel(s_root, 508, 94, 276, 230, 14);
  s_nextCard = nextCard;
  lv_obj_add_flag(nextCard, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(nextCard, onNextCard, LV_EVENT_SHORT_CLICKED, nullptr);
  s_nextEdge = lv_obj_create(nextCard);
  lv_obj_remove_style_all(s_nextEdge);
  lv_obj_set_size(s_nextEdge, EDGE_W, 60);
  lv_obj_set_pos(s_nextEdge, 0, 22);
  lv_obj_set_style_bg_opa(s_nextEdge, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(s_nextEdge, 2, 0);

  // Standings, news and settings were reachable only from the board — and the
  // board is not what is on screen for most of the day. The same three buttons
  // belong here.
  auto navBtn = [&](int x, const char* text, lv_event_cb_t cb) {
    lv_obj_t* b = lv_btn_create(s_root);
    lv_obj_set_size(b, 44, 36);
    lv_obj_set_pos(b, x, 6);
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
  };
  navBtn(SCR_W - 18 - 44,  "SET",  onIdleSettings);
  navBtn(SCR_W - 18 - 96,  "NEWS", onIdleNews);
  navBtn(SCR_W - 18 - 148, "TBL",  onIdleTable);

  lv_obj_t* nextHdr = lbl(nextCard, 24, 22, C_INK3, F_MICRO);
  lv_obj_set_style_text_letter_space(nextHdr, 1, 0);
  lv_label_set_text(nextHdr, "NEXT UP");

  // Same geometry rules as the board: no explicit size on the image, because
  // in LVGL 8.3 that CLIPS the source instead of scaling it. SIZE_MODE_REAL
  // plus a 0,0 pivot lands it exactly where the badge would be.
  auto logoAt = [&](int x, int y) {
    lv_obj_t* im = lv_img_create(nextCard);
    lv_img_set_antialias(im, true);
    lv_img_set_zoom(im, (uint16_t)((256 * 34) / 48));
    lv_img_set_pivot(im, 0, 0);
    lv_img_set_size_mode(im, LV_IMG_SIZE_MODE_REAL);
    lv_obj_set_pos(im, x, y);
    lv_obj_add_flag(im, LV_OBJ_FLAG_HIDDEN);
    return im;
  };

  s_nextBadgeA = teamBadge(nextCard, "", 0x5D6D7E, 34);
  lv_obj_set_pos(s_nextBadgeA, 24, 56);
  s_nextLblA = lv_obj_get_child(s_nextBadgeA, 0);
  s_nextLogoA = logoAt(24, 56);
  s_nextAway = lbl(nextCard, 66, 64, C_INK, F_ABBR);

  s_nextHome = lbl(nextCard, 148, 64, C_INK2, F_ABBR);
  s_nextBadgeH = teamBadge(nextCard, "", 0x5D6D7E, 34);
  lv_obj_set_pos(s_nextBadgeH, 206, 56);
  s_nextLblH = lv_obj_get_child(s_nextBadgeH, 0);
  s_nextLogoH = logoAt(206, 56);

  // The countdown is the reason this screen exists, so it gets the hero face.
  // F_HERO covers digits plus '-' ':' 'H' 'M' and NOTHING ELSE — the "NOW"
  // case swaps to F_DISPLAY at write time in uiIdleTick(). A missing glyph is
  // a silent hollow box in LVGL and three shipped bugs have been exactly that.
  s_countdown = lbl(nextCard, 24, 100, C_LIVE, F_HERO);
  s_nextMeta  = lbl(nextCard, 24, 194, C_INK3, F_NUM);
  s_nextNone  = lbl(nextCard, 24, 110, C_INK2, F_BODY);
  lv_label_set_text(s_nextNone, "Nothing scheduled");
  lv_obj_add_flag(s_nextNone, LV_OBJ_FLAG_HIDDEN);

  // ── the ledger ───────────────────────────────────────────────────────────
  //
  // Two columns of rows on the bare plate, under one hairline each. Same
  // reasoning as the board's ledger (see ui_ledger.cpp): a list of fixtures is
  // a list, not an object, and giving it a fill was most of what made this
  // screen 80.5% one colour.
  //
  // The geometry is duplicated rather than shared with ui_ledger, deliberately.
  // That module holds file-static state for exactly one instance, and coupling
  // two screens through it — parenting a single tree that has to move between
  // them — is a worse bug than thirty lines of repeated layout.
  auto ruleAt = [&](int x) {
    lv_obj_t* r = lv_obj_create(s_root);
    lv_obj_remove_style_all(r);
    lv_obj_set_pos(r, x, 368);
    lv_obj_set_size(r, 348, 1);
    lv_obj_set_style_bg_color(r, C_LINE, 0);
    lv_obj_set_style_bg_opa(r, OPA_HAIR, 0);
    return r;
  };
  auto hdrAt = [&](int x, const char* text) {
    lv_obj_t* h = lbl(s_root, x, 348, C_INK3, F_MICRO);
    lv_obj_set_style_text_letter_space(h, 1, 0);
    lv_label_set_text(h, text);
    return h;
  };

  // Kept so uiIdleRefresh() can hide a column whole. A heading ruled over
  // nothing is the same "failed to load" signal a bordered empty card gives,
  // and on a July Tuesday BOTH columns are empty.
  s_ledHdr[0] = hdrAt(24, "TODAY");
  s_ledRule[0] = ruleAt(24);
  s_ledHdr[1] = hdrAt(412, "LATEST");
  s_ledRule[1] = ruleAt(412);

  // Transparent hit targets over each ledger row. The rows are bare labels on
  // the plate, and a label is not clickable and is only as wide as its text —
  // so there was nothing to press even where the data was worth opening.
  // 28 px tall on a 30 px pitch, which clears the 9 mm ISO 9241-411 floor in
  // the axis that matters here.
  auto hit = [&](int x, int y, int i, lv_event_cb_t cb) {
    lv_obj_t* h = lv_obj_create(s_root);
    lv_obj_remove_style_all(h);
    lv_obj_set_pos(h, x, y - 4);
    lv_obj_set_size(h, 348, 28);
    lv_obj_set_style_bg_opa(h, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(h, 6, 0);
    // Visible only while held. The rows must not look like buttons at rest —
    // the ledger is a list, and 27 panels that light up on touch was already
    // flagged once as making inert things look interactive.
    lv_obj_set_style_bg_color(h, C_LINE, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(h, 24, LV_STATE_PRESSED);
    lv_obj_clear_flag(h, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(h, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(h, cb, LV_EVENT_SHORT_CLICKED, (void*)(intptr_t)i);
  };

  for (int i = 0; i < IDLE_ROWS; i++) {
    const int y = 384 + i * 30;
    hit(24,  y, i, onTodayRow);
    hit(412, y, i, onFinalRow);
    s_todayTime[i]  = lbl(s_root, 24,  y, C_INK3, F_NUM);
    s_todayGame[i]  = lbl(s_root, 98,  y, C_INK2, F_BODY);
    s_todayLg[i]    = lbl(s_root, 238, y, C_INK3, F_NUM, LV_TEXT_ALIGN_RIGHT, 134);
    s_finalGame[i]  = lbl(s_root, 412, y, C_INK2, F_BODY);
    s_finalScore[i] = lbl(s_root, 626, y, C_INK3, F_NUM, LV_TEXT_ALIGN_RIGHT, 134);
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
  // Placed from the CLOCK's rendered width, every time it changes — "9:14" and
  // "11:37" are not the same width, so a fixed x is wrong for half the day.
  // Baselines are aligned through each face's own metrics rather than by eye:
  // a 96 px face and a 30 px face share no other reference point.
  lv_label_set_text(s_ampm, lt.tm_hour < 12 ? "AM" : "PM");
  {
    // Measured from the TEXT, not from the object. lv_obj_get_width() is not
    // valid until layout has run, and on the first tick it returns 0 — which a
    // width cache then locks in, stamping "AM" straight through the digits.
    static int lastW = -1;
    const int w = (int)lv_txt_get_width(s_cClock, (uint32_t)strlen(s_cClock),
                                        F_CLOCK, 0, LV_TEXT_FLAG_NONE);
    if (w != lastW) {
      lastW = w;
      const int blClock = 68 + (int)lv_font_get_line_height(F_CLOCK)
                             - (int)F_CLOCK->base_line;
      const int topAmpm = blClock - ((int)lv_font_get_line_height(F_DISPLAY)
                                   - (int)F_DISPLAY->base_line);
      lv_obj_set_pos(s_ampm, 44 + w + 18, topAmpm);
    }
  }
  // Tracked CAPS. The rule this build adopts is that +1 tracking with capitals
  // means "this labels something" and zero tracking means "this is the data" —
  // so a tracked mixed-case string is neither, and reads as a typo.
  strftime(buf, sizeof buf, "%A  ·  %B %e", &lt);
  for (char* p = buf; *p; p++) *p = (char)toupper((unsigned char)*p);
  setCached(s_date, s_cDate, sizeof s_cDate, buf);

  const Game* nx = nextGame();
  lv_obj_t* const matchup[] = { s_nextBadgeA, s_nextBadgeH, s_nextAway, s_nextHome };
  if (!nx) {
    // The whole card goes, not just its contents. A 276x230 panel holding one
    // line of small text is the same "content failed to arrive" signal as an
    // empty bordered region — and this is now the ONLY card on the screen, so
    // it carries that signal for the entire display. A clock on a bare plate
    // is a complete answer to "nothing is scheduled".
    lv_obj_add_flag(s_nextCard, LV_OBJ_FLAG_HIDDEN);
    setCached(s_nextMeta, s_cMeta, sizeof s_cMeta, "");
    s_cNextId[0] = '\0';
    return;
  }
  lv_obj_clear_flag(s_nextCard, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(s_nextNone, LV_OBJ_FLAG_HIDDEN);
  for (lv_obj_t* o : matchup) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(s_countdown, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(s_nextEdge, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(s_nextNone, LV_OBJ_FLAG_HIDDEN);

  if (strncmp(s_cNextId, nx->id, sizeof s_cNextId - 1) != 0) {
    strncpy(s_cNextId, nx->id, sizeof s_cNextId - 1);
    lv_label_set_text(s_nextAway, nx->away.abbr);
    lv_label_set_text(s_nextHome, nx->home.abbr);
  }
  // Outside the id cache on purpose: a logo arrives LATER than the fixture it
  // belongs to, so gating this on "the game changed" left the badge showing
  // until the next fixture came round.
  {
    const lv_img_dsc_t* la = logoGet(nx->league, nx->away.abbr);
    const lv_img_dsc_t* lh = logoGet(nx->league, nx->home.abbr);
    if (la) { lv_img_set_src(s_nextLogoA, la); lv_obj_clear_flag(s_nextLogoA, LV_OBJ_FLAG_HIDDEN); }
    else    { lv_obj_add_flag(s_nextLogoA, LV_OBJ_FLAG_HIDDEN); }
    if (lh) { lv_img_set_src(s_nextLogoH, lh); lv_obj_clear_flag(s_nextLogoH, LV_OBJ_FLAG_HIDDEN); }
    else    { lv_obj_add_flag(s_nextLogoH, LV_OBJ_FLAG_HIDDEN); }
    la ? lv_obj_add_flag(s_nextBadgeA, LV_OBJ_FLAG_HIDDEN)
       : lv_obj_clear_flag(s_nextBadgeA, LV_OBJ_FLAG_HIDDEN);
    lh ? lv_obj_add_flag(s_nextBadgeH, LV_OBJ_FLAG_HIDDEN)
       : lv_obj_clear_flag(s_nextBadgeH, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_nextLblA, nx->away.abbr);
    lv_label_set_text(s_nextLblH, nx->home.abbr);
    // Through the normaliser, like everywhere else: setting bg_color directly
    // leaves the label ink unmatched, and a Pittsburgh badge a hole in the card.
    teamBadgeSet(s_nextBadgeA, nx->away.color);
    teamBadgeSet(s_nextBadgeH, nx->home.color);
    lv_obj_set_style_bg_color(s_nextEdge,
        nx->isFav ? lv_color_hex(teamInk(nx->home.color)) : C_EDGE, 0);
  }

  // Countdown is the hero — it is the reason this screen exists.
  //
  // Units are CAPS because neither large face has lowercase (theme.h). F_HERO
  // is narrower still: digits, '-', ':', 'H' and 'M' only. So the two spellings
  // that fall outside it — "NOW" and the "%ldD" day form — swap to F_DISPLAY,
  // which covers the full caps range at 30 px. Getting this wrong does not
  // fail loudly; LVGL just draws hollow boxes.
  const long secs = (long)nx->startUtc - (long)now;
  bool heroFace = true;
  if (secs <= 0) {
    snprintf(buf, sizeof buf, "NOW");
    heroFace = false;
  } else if (secs < 3600) {
    snprintf(buf, sizeof buf, "%ldM", secs / 60);
  } else if (secs < 86400) {
    snprintf(buf, sizeof buf, "%ldH %ldM", secs / 3600, (secs % 3600) / 60);
  } else {
    snprintf(buf, sizeof buf, "%ldD", secs / 86400);
    heroFace = false;                       // 'D' is not in F_HERO
  }
  const lv_font_t* want = heroFace ? F_HERO : F_DISPLAY;
  if (lv_obj_get_style_text_font(s_countdown, LV_PART_MAIN) != want)
    lv_obj_set_style_text_font(s_countdown, want, 0);
  setCached(s_countdown, s_cCountdown, sizeof s_cCountdown, buf);

  struct tm st;
  const time_t startT = (time_t)nx->startUtc;
  localtime_r(&startT, &st);
  char tbuf[16];
  clockFormat(st, tbuf, sizeof tbuf);
  char lg[8];
  size_t li = 0;
  for (; nx->league[li] && li < sizeof lg - 1; li++)
    lg[li] = (char)toupper((unsigned char)nx->league[li]);
  lg[li] = '\0';
  snprintf(buf, sizeof buf, "%s   %s%s%s", tbuf,
           lg, nx->bcast[0] ? "   " : "", nx->bcast);
  setCached(s_nextMeta, s_cMeta, sizeof s_cMeta, buf);
}

void uiIdleRefresh() {
  if (!s_root) return;

  uint8_t today = 0;
  for (uint8_t i = 0; i < g_gameCount; i++)
    if (g_board[i].state == GS_PRE) today++;

  const Game* nx = nextGame();
  // Resolve the pointer back to an index so a tap can find it again. Every
  // tappable thing on this screen is rebuilt here, because g_board is replaced
  // wholesale on each poll and an index captured earlier would be stale.
  s_nextIdx = -1;
  if (nx) s_nextIdx = (int8_t)(nx - g_board);

  char buf[64];
  if (nx) {
    struct tm st;
    const time_t t = (time_t)nx->startUtc;
    localtime_r(&t, &st);
    char tb[16];
    clockFormat(st, tb, sizeof tb);
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
    if (g_set.clock24) {
      strftime(tb, sizeof tb, "%H:%M", &st);
      lv_label_set_text(s_todayTime[row], tb);
    } else {
      // The meridiem is dropped here on purpose: the column is narrow and the
      // list is all within one day, so AM/PM adds width without adding sense.
      strftime(tb, sizeof tb, "%l:%M", &st);
      lv_label_set_text(s_todayTime[row], tb[0] == ' ' ? tb + 1 : tb);
    }
    // A golf field or an F1 grid has no two sides, so the "A @ B" shape would
    // print a bare "@" with nothing either side of it. The event has a name;
    // use it.
    if (g.model == SM_LEADERBOARD || g.model == SM_GRID)
      snprintf(buf, sizeof buf, "%s", g.away.name[0] ? g.away.name : g.home.name);
    else
      snprintf(buf, sizeof buf, "%s  %s  %s", g.away.abbr, g.isFav ? "vs" : "@", g.home.abbr);
    lv_label_set_text(s_todayGame[row], buf);
    lv_label_set_text(s_todayLg[row], g.league);
    s_rowIdx[row] = (int8_t)i;
    row++;
  }
  const uint8_t todayRows = row;
  for (; row < IDLE_ROWS; row++) {
    lv_label_set_text(s_todayTime[row], "");
    lv_label_set_text(s_todayGame[row], "");
    lv_label_set_text(s_todayLg[row], "");
    s_rowIdx[row] = -1;                 // an empty row must not open anything
  }

  row = 0;
  for (uint8_t i = 0; i < g_gameCount && row < IDLE_ROWS; i++) {
    const Game& g = g_board[i];
    if (g.state != GS_FINAL) continue;
    const bool field = (g.model == SM_LEADERBOARD || g.model == SM_GRID);
    if (field) {
      // A finished tournament is worth a line, but "0 - 0" is not a score it
      // ever had — that empty row was what a completed golf event looked like.
      snprintf(buf, sizeof buf, "%s", g.away.name[0] ? g.away.name : g.home.name);
      lv_label_set_text(s_finalGame[row], buf);
      lv_label_set_text(s_finalScore[row], "");
    } else {
      snprintf(buf, sizeof buf, "%s  @  %s", g.away.abbr, g.home.abbr);
      lv_label_set_text(s_finalGame[row], buf);
      snprintf(buf, sizeof buf, "%u - %u", g.away.score, g.home.score);
      lv_label_set_text(s_finalScore[row], buf);
    }
    s_finIdx[row] = (int8_t)i;
    row++;
  }
  const uint8_t finalRows = row;
  for (; row < IDLE_ROWS; row++) {
    lv_label_set_text(s_finalGame[row], "");
    lv_label_set_text(s_finalScore[row], "");
    s_finIdx[row] = -1;
  }

  // A ruled heading with nothing under it reads as content that failed to
  // arrive. Both columns are empty on a July Tuesday, which is precisely when
  // this screen is up.
  const uint8_t filled[2] = { todayRows, finalRows };
  for (int c = 0; c < 2; c++) {
    const lv_opa_t o = filled[c] ? LV_OPA_COVER : LV_OPA_TRANSP;
    lv_obj_set_style_opa(s_ledHdr[c], o, 0);
    lv_obj_set_style_opa(s_ledRule[c], o, 0);
  }

  uiIdleTick();
}
