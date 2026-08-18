// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
// ui_alert.cpp — the score takeover. UI.md §8.
//
// A 520x300 card is 156,000 px. An opacity tween on that at 30 fps is not
// affordable against a panel DMA that already wants ~32 MB/s, so the card
// composites once, fades in over FOUR DISCRETE STEPS, then holds completely
// static. The only thing that moves afterwards is a 200x6 pulse bar.
//
// Do not "improve" this with a slide-in.
//
// ── who gets the takeover ──────────────────────────────────────────────────
//
// The full-screen card was designed for a panel across a room, where a goal is
// a glance you either catch or you do not. This one sits on a desk, beside a
// monitor being worked at — and the card hides the other eight games for ten
// seconds, potentially every twelve. At that distance an unrequested takeover
// is not ambient, it is an interruption.
//
// So it is now earned rather than automatic. A team you follow keeps the whole
// card. Everyone else gets an 800x44 banner and a flare on the scoring tile's
// edge light: nothing is occluded, and it repaints 35,200 px instead of
// 156,000 — cheaper than what it replaces.
#include "ui.h"
#include "theme.h"
#include "../config.h"
#include "../core/state.h"

static lv_obj_t* s_scrim;
static lv_obj_t* s_card;
static lv_obj_t* s_edge;
static lv_obj_t* s_badge;
static lv_obj_t* s_badgeLbl;
static lv_obj_t* s_verb;
static lv_obj_t* s_who;
static lv_obj_t* s_detail;
static lv_obj_t* s_awayAbbr;
static lv_obj_t* s_awayScore;
static lv_obj_t* s_homeAbbr;
static lv_obj_t* s_homeScore;
static lv_obj_t* s_status;
static lv_obj_t* s_pulse;
static lv_obj_t* s_hint;

// The non-occluding path.
static lv_obj_t* s_banner;
static lv_obj_t* s_banEdge;
static lv_obj_t* s_banAbbr;
static lv_obj_t* s_banVerb;
static lv_obj_t* s_banWho;
static lv_obj_t* s_banScore;
static uint32_t  s_banUntil;

// Queue: a three-goal burst must not stack four cards on top of each other.
static AlertEvent s_queue[MAX_EVENTS];
static uint8_t    s_qHead, s_qCount;
static bool       s_showing;
static uint32_t   s_shownAt;
static uint32_t   s_lastDismiss;
static uint8_t    s_fadeStep;
static uint32_t   s_lastFade;
/** Sequence of the event currently on screen — committed only once it has
 *  actually been seen. See uiAlertPending(). */
static uint32_t   s_liveSeq;

lv_obj_t* uiAlertRoot() { return s_scrim; }

static lv_obj_t* lbl(lv_obj_t* p, int x, int y, lv_color_t c, const lv_font_t* f,
                     lv_text_align_t a = LV_TEXT_ALIGN_LEFT, int w = 0) {
  lv_obj_t* l = lv_label_create(p);
  lv_obj_set_pos(l, x, y);
  lv_obj_set_style_text_color(l, c, 0);
  lv_obj_set_style_text_font(l, f, 0);
  if (w) { lv_obj_set_width(l, w); lv_obj_set_style_text_align(l, a, 0); }
  lv_label_set_text(l, "");
  return l;
}

static void onDismiss(lv_event_t*) { uiAlertDismiss(); }

void uiAlertInit(lv_obj_t* parent) {
  s_scrim = lv_obj_create(parent);
  lv_obj_remove_style_all(s_scrim);
  lv_obj_set_size(s_scrim, SCR_W, SCR_H);
  lv_obj_set_pos(s_scrim, 0, 0);
  lv_obj_set_style_bg_color(s_scrim, lv_color_hex(0x04070C), 0);
  lv_obj_set_style_bg_opa(s_scrim, 158, 0);          // ~62%
  lv_obj_clear_flag(s_scrim, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_scrim, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(s_scrim, LV_OBJ_FLAG_HIDDEN);
  // Clickable both to dismiss and to stop taps falling through to the board.
  lv_obj_add_event_cb(s_scrim, onDismiss, LV_EVENT_CLICKED, nullptr);

  s_card = glassPanel(s_scrim, ALERT_X, ALERT_Y, ALERT_W, ALERT_H, 14);

  s_edge = lv_obj_create(s_card);
  lv_obj_remove_style_all(s_edge);
  lv_obj_set_size(s_edge, 6, ALERT_H - 2);
  lv_obj_set_pos(s_edge, 0, 0);
  lv_obj_set_style_bg_opa(s_edge, LV_OPA_COVER, 0);

  s_badge = teamBadge(s_card, "", 0x5D6D7E, 86);
  lv_obj_set_pos(s_badge, 34, 28);
  lv_obj_set_style_radius(s_badge, 14, 0);
  s_badgeLbl = lv_obj_get_child(s_badge, 0);

  // The verb is the largest type anywhere in the product, deliberately — and
  // it is TEXT, so it cannot use F_SCORE. It did, and "GOAL" rendered as four
  // hollow boxes on every alert the product has ever raised.
  s_verb   = lbl(s_card, 146, 30, C_INK, F_DISPLAY);
  s_who    = lbl(s_card, 146, 78, C_INK, F_BODY);
  s_detail = lbl(s_card, 146, 100, C_INK3, F_BODY);   // assist names carry accents

  s_awayAbbr  = lbl(s_card, 40, 168, C_INK3, F_MICRO);
  s_awayScore = lbl(s_card, 40, 186, C_INK2, F_SCORE);
  s_homeAbbr  = lbl(s_card, 150, 168, C_INK, F_MICRO);
  s_homeScore = lbl(s_card, 150, 186, C_INK, F_SCORE);
  s_status    = lbl(s_card, ALERT_W - 40 - 200, 190, C_INK2, F_MICRO,
                    LV_TEXT_ALIGN_RIGHT, 200);

  s_pulse = lv_obj_create(s_card);
  lv_obj_remove_style_all(s_pulse);
  lv_obj_set_size(s_pulse, 200, 6);
  lv_obj_set_pos(s_pulse, 40, ALERT_H - 26);
  lv_obj_set_style_radius(s_pulse, 3, 0);
  lv_obj_set_style_bg_opa(s_pulse, LV_OPA_COVER, 0);

  s_hint = lbl(s_card, ALERT_W - 34 - 200, ALERT_H - 24, C_INK3, F_MICRO,
               LV_TEXT_ALIGN_RIGHT, 200);
  lv_label_set_text(s_hint, "TAP OR 10s TO DISMISS");

  // ── the banner ───────────────────────────────────────────────────────────
  // Same information, no occlusion. Sits over the top bar, which is the least
  // valuable 44 px on the board.
  s_banner = glassPanel(parent, 0, 0, SCR_W, 44, 0);
  lv_obj_add_flag(s_banner, LV_OBJ_FLAG_HIDDEN);

  s_banEdge = lv_obj_create(s_banner);
  lv_obj_remove_style_all(s_banEdge);
  lv_obj_set_size(s_banEdge, 4, 44);
  lv_obj_set_pos(s_banEdge, 0, 0);
  lv_obj_set_style_bg_opa(s_banEdge, LV_OPA_COVER, 0);

  s_banAbbr  = lbl(s_banner, 20, 13, C_INK, F_ABBR);
  s_banVerb  = lbl(s_banner, 92, 13, C_INK, F_ABBR);
  s_banWho   = lbl(s_banner, 230, 14, C_INK2, F_BODY);   // player names
  s_banScore = lbl(s_banner, SCR_W - 20 - 150, 13, C_INK, F_NUM,
                   LV_TEXT_ALIGN_RIGHT, 150);
}

/** True when the side that scored is one the user actually follows. */
static bool scoredByFavourite(const AlertEvent& e) {
  for (uint8_t i = 0; i < g_gameCount; i++) {
    const Game& g = g_board[i];
    if (strcmp(g.id, e.gameId) != 0) continue;
    const bool home = strcmp(g.home.abbr, e.abbr) == 0;
    return sideIsFav(g.league, home ? g.home.id : g.away.id);
  }
  return false;
}

static void presentBanner(const AlertEvent& e) {
  // Lifted against the card this actually sits on, not the default tile.
  const uint32_t ink = teamInkFor(e.color, kStateInk[SI_HERO].fill);
  lv_obj_set_style_bg_color(s_banEdge, lv_color_hex(ink), 0);
  lv_label_set_text(s_banAbbr, e.abbr);
  lv_label_set_text(s_banVerb, e.verb);
  lv_label_set_text(s_banWho, e.who[0] ? e.who : "");
  char sc[24];
  snprintf(sc, sizeof sc, "%u - %u", e.scoreAway, e.scoreHome);
  lv_label_set_text(s_banScore, sc);

  lv_obj_clear_flag(s_banner, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(s_banner);
  s_banUntil = millis() + ALERT_BANNER_MS;
  uiBoardFlash(e.gameId);
}

bool uiAlertActive() { return s_showing; }

/**
 * The sequence the device may safely commit to NVS: everything strictly before
 * the event on screen and before anything still queued. Committing further
 * would swallow an alert if the panel lost power mid-card.
 */
uint32_t uiAlertSafeSeq(uint32_t proxySeq) {
  uint32_t lowest = proxySeq;
  if (s_showing && s_liveSeq) lowest = min(lowest, s_liveSeq - 1);
  for (uint8_t i = 0; i < s_qCount; i++) {
    const uint32_t q = s_queue[(s_qHead + i) % MAX_EVENTS].seq;
    if (q) lowest = min(lowest, q - 1);
  }
  return lowest;
}

void uiAlertEnqueue(const AlertEvent& e) {
  if (!g_set.alertsOn || inQuietHours()) return;
  if (s_qCount >= MAX_EVENTS) return;                     // drop rather than wrap
  s_queue[(s_qHead + s_qCount) % MAX_EVENTS] = e;
  s_qCount++;
}

static void present(const AlertEvent& e) {
  lv_obj_set_style_bg_color(s_edge, lv_color_hex(teamInkFor(e.color, kStateInk[SI_HERO].fill)), 0);
  teamBadgeSet(s_badge, e.color);
  // Was the RAW wire colour with no lift whatsoever: 24 of 34 kits measured
  // below 3:1 here, Toronto at 1.01:1 — invisible — on the goal takeover the
  // whole alert system exists to deliver.
  lv_obj_set_style_bg_color(s_pulse, lv_color_hex(teamInkFor(e.color, kStateInk[SI_HERO].fill)), 0);
  lv_label_set_text(s_badgeLbl, e.abbr);
  lv_label_set_text(s_verb, e.verb);
  lv_label_set_text(s_who, e.who[0] ? e.who : "");
  lv_label_set_text(s_detail, e.detail[0] ? e.detail : "");
  lv_label_set_text(s_status, e.status);

  // Whichever side scored takes the ink; the other drops to ink-2.
  const Game* g = nullptr;
  for (uint8_t i = 0; i < g_gameCount; i++)
    if (strcmp(g_board[i].id, e.gameId) == 0) { g = &g_board[i]; break; }
  const bool homeScored = g && strcmp(g->home.abbr, e.abbr) == 0;

  lv_label_set_text(s_awayAbbr, g ? g->away.abbr : "");
  lv_label_set_text(s_homeAbbr, g ? g->home.abbr : "");
  char b[8];
  snprintf(b, sizeof b, "%u", e.scoreAway); lv_label_set_text(s_awayScore, b);
  snprintf(b, sizeof b, "%u", e.scoreHome); lv_label_set_text(s_homeScore, b);
  lv_obj_set_style_text_color(s_awayScore, homeScored ? C_INK2 : C_INK, 0);
  lv_obj_set_style_text_color(s_homeScore, homeScored ? C_INK : C_INK2, 0);

  s_liveSeq  = e.seq;
  s_showing  = true;
  s_shownAt  = millis();
  s_fadeStep = 0;
  s_lastFade = 0;
  lv_obj_set_style_opa(s_scrim, 64, 0);
  lv_obj_clear_flag(s_scrim, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(s_scrim);
}

void uiAlertDismiss() {
  if (!s_showing) return;
  s_showing = false;
  s_liveSeq = 0;
  s_lastDismiss = millis();
  lv_obj_add_flag(s_scrim, LV_OBJ_FLAG_HIDDEN);
}

void uiAlertTick() {
  if (!s_scrim) return;

  if (s_showing) {
    // Four discrete steps, ~70 ms apart. Not a tween — see the file header.
    if (s_fadeStep < ALERT_FADE_STEPS && millis() - s_lastFade > 70) {
      s_lastFade = millis();
      s_fadeStep++;
      const lv_opa_t opa = (lv_opa_t)(64 + (LV_OPA_COVER - 64) * s_fadeStep / ALERT_FADE_STEPS);
      lv_obj_set_style_opa(s_scrim, opa, 0);
    }
    if (millis() - s_shownAt > ALERT_HOLD_MS) uiAlertDismiss();
    return;
  }

  if (s_banUntil && millis() >= s_banUntil) {
    s_banUntil = 0;
    lv_obj_add_flag(s_banner, LV_OBJ_FLAG_HIDDEN);
  }

  if (!s_qCount) return;
  if (millis() - s_lastDismiss < ALERT_GAP_MS && s_lastDismiss) return;
  const AlertEvent e = s_queue[s_qHead];
  s_qHead = (s_qHead + 1) % MAX_EVENTS;
  s_qCount--;
  // Earned interruptions only — see the header.
  if (scoredByFavourite(e)) present(e);
  else                      presentBanner(e);
}
