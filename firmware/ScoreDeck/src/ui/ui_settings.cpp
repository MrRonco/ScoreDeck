// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
// ui_settings.cpp — the settings the panel is genuinely better at.
//
// WHAT IS HERE AND WHAT IS NOT
//
// The original split was "the panel edits anything expressible without
// typing". That was the wrong test. The argument was never that this panel is
// hard to type on — it is that a far better input device is permanently two
// feet away. Whenever you are near this screen you are already sitting at a
// keyboard, so the panel's claim on any set-once setting is zero.
//
// The test that replaced it:
//
//   Does this get touched more than twice a year, or is it needed when the
//   browser is unreachable? Everything else is browser-only.
//
// So region, timezone, refresh cadence, clock format and the alert-on-start /
// alert-on-final toggles all live in the browser. Leagues too — the board's
// league strip already filters per session, which covers the whole
// in-the-moment case, and persistent league choice was a set-once setting
// wearing a costume. That deletion is what lets the device keep never knowing
// the league registry.
//
// What stays is: things you change while looking at the board (density), mutes
// you want to hit NOW rather than schedule (alerts, quiet hours), favourites
// reordering, and the recovery paths that must work when the network does not.
//
// TWO HARDWARE FACTS THIS DESIGN IS BUILT ON
//
//   * LVGL objects live in PSRAM here (lv_conf.h sets LV_MEM_CUSTOM with
//     ps_malloc), so this whole screen costs ~30 KB of an 8 MB pool, not of
//     the ~120 KB internal heap the TLS gate protects. Build once, never
//     rebuild; the real cost of object count is render time, not memory.
//
//   * Every NVS write stalls the panel 150-220 ms regardless of size
//     (INHERITED_RULES.md §16). Saving on each toggle would visibly shake the
//     screen five times while you flipped five switches. Writes are debounced
//     and flushed on exit — see uiSettingsTick().
#include "ui.h"
#include "theme.h"
#include "../config.h"
#include "../core/state.h"
#include "../net/api.h"
#include "../svc/web.h"
#include "../net/api.h"
#include <WiFi.h>

// Four panes, not three. The split of WHAT belongs on the panel has not
// changed — this is a layout consequence: adding the clock row left 46 px
// under the last setting, which is not a list. Favourites get their own card
// and room to show full team names.
#define PANE_COUNT 5
#define ROW_H      56
#define ROW_PITCH  57
#define ROWS_MAX    6
#define FAV_ROWS    8

static lv_obj_t* s_root;
static lv_obj_t* s_seg[PANE_COUNT];
static lv_obj_t* s_segLbl[PANE_COUNT];
static lv_obj_t* s_pane[PANE_COUNT];
static lv_obj_t* s_dirtyDot;
static uint8_t   s_tab;

// BOARD pane
static lv_obj_t* s_denSeg[DEN_COUNT];
static lv_obj_t* s_swAlerts;
static lv_obj_t* s_swFocus;
static lv_obj_t* s_swQuiet;
static lv_obj_t* s_clkSeg[2];
static lv_obj_t* s_favRow[FAV_ROWS];
static lv_obj_t* s_favSwatch[FAV_ROWS];
static lv_obj_t* s_favLbl[FAV_ROWS];
static lv_obj_t* s_favCount;
static uint8_t   s_favPage;

// NETWORK / SYSTEM panes
static lv_obj_t* s_netVal[5];
static lv_obj_t* s_sysVal[5];
static lv_obj_t* s_testLbl;

// Timezone picker — a sub-view over the NETWORK pane rather than a row, since
// 22 cities do not fit in a 56 px row and a POSIX rule is not something anyone
// should have to type on a touchscreen.
#define TZ_COLS 2
#define TZ_ROWS 7
#define TZ_PER  (TZ_COLS * TZ_ROWS)
static lv_obj_t* s_tzView;
static lv_obj_t* s_tzBtn[TZ_PER];
static lv_obj_t* s_tzBtnLbl[TZ_PER];
static lv_obj_t* s_tzTick[TZ_PER];
static lv_obj_t* s_tzPageLbl;
static uint8_t   s_tzPage;

// Debounced save — see the file header.
static bool     s_dirty;
static bool     s_leaguesDirty;   // a league change forces a poll on flush
static uint32_t s_dirtyAt;

lv_obj_t* uiSettingsRoot() { return s_root; }

static void markDirty() {
  s_dirty = true;
  s_dirtyAt = millis();
  if (s_dirtyDot) lv_obj_clear_flag(s_dirtyDot, LV_OBJ_FLAG_HIDDEN);
}

static void flushNow() {
  if (!s_dirty) return;
  s_dirty = false;
  settingsSave();
  if (s_leaguesDirty) { s_leaguesDirty = false; webPollNow(); }
  if (s_dirtyDot) lv_obj_add_flag(s_dirtyDot, LV_OBJ_FLAG_HIDDEN);
}

// ── small builders ─────────────────────────────────────────────────────────
static lv_obj_t* label(lv_obj_t* p, int x, int y, const char* t,
                       lv_color_t c, const lv_font_t* f) {
  lv_obj_t* l = lv_label_create(p);
  lv_obj_set_pos(l, x, y);
  lv_label_set_text(l, t);
  lv_obj_set_style_text_color(l, c, 0);
  lv_obj_set_style_text_font(l, f, 0);
  return l;
}

/** A settings row. The WHOLE row is the touch target — the control inside is
 *  visual only, which is what keeps every target 736x56 rather than 52x28. */
static lv_obj_t* row(lv_obj_t* pane, int idx, const char* title, lv_event_cb_t cb) {
  lv_obj_t* r = lv_obj_create(pane);
  lv_obj_remove_style_all(r);
  lv_obj_set_size(r, 736, ROW_H);
  lv_obj_set_pos(r, 16, 16 + idx * ROW_PITCH);
  lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
  uiPressable(r);
  lv_obj_set_style_radius(r, R_MD, 0);
  lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
  if (cb) {
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(r, cb, LV_EVENT_SHORT_CLICKED, nullptr);
  }
  if (idx) {
    lv_obj_t* rule = lv_obj_create(pane);
    lv_obj_remove_style_all(rule);
    lv_obj_set_size(rule, 736, 1);
    lv_obj_set_pos(rule, 16, 16 + idx * ROW_PITCH - 1);
    lv_obj_set_style_bg_color(rule, C_EDGE, 0);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
  }
  label(r, 8, (ROW_H - 15) / 2, title, C_INK, F_BODY);
  return r;
}

/** A switch that is drawn, not interactive — its row owns the event. */
static lv_obj_t* switchAt(lv_obj_t* r, bool on) {
  lv_obj_t* sw = lv_obj_create(r);
  lv_obj_remove_style_all(sw);
  lv_obj_set_size(sw, 52, 28);
  lv_obj_set_pos(sw, 736 - 8 - 52, (ROW_H - 28) / 2);
  lv_obj_set_style_radius(sw, 14, 0);
  lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, 0);
  lv_obj_clear_flag(sw, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* knob = lv_obj_create(sw);
  lv_obj_remove_style_all(knob);
  lv_obj_set_size(knob, 22, 22);
  lv_obj_set_style_radius(knob, 11, 0);
  lv_obj_set_style_bg_opa(knob, LV_OPA_COVER, 0);
  (void)on;
  return sw;
}

static void switchSet(lv_obj_t* sw, bool on) {
  lv_obj_set_style_bg_color(sw, on ? C_EDGE_HI : C_EDGE, 0);
  lv_obj_t* knob = lv_obj_get_child(sw, 0);
  if (!knob) return;
  lv_obj_set_pos(knob, on ? 27 : 3, 3);
  lv_obj_set_style_bg_color(knob, on ? C_INK : C_INK3, 0);
}

// ── events ─────────────────────────────────────────────────────────────────

// ── SPORTS — which leagues fill the board beyond the favourites ────────────
//
// The invariant this pane breaks, and how it is honoured: the device still
// ships knowing NO leagues. The registry arrives from GET /v1/catalog
// (leagues-only, ~1.3 KB) when the pane first opens, cached for the session;
// offline, the pane degrades to (stored CSV ∪ tonight's board) with a note.
//
// The cap is real and asymmetric — api.cpp stops parsing league counts at
// MAX_LEAGUES(12), so a 13th selection would silently vanish from the header.
// The meter therefore counts AUTO leagues (kept on by a favourite) against
// the 12, drawn dimmer and first, so "four of my twelve are spoken for" is
// visible rather than discovered.
#define SP_FAMS   8
#define SP_SLOTS  8            // 2 cols x 4 rows per page

static lv_obj_t* s_spMeterLbl;
static lv_obj_t* s_spTick[MAX_LEAGUES];
static lv_obj_t* s_spMsg;                 // cap warning / AUTO explanation / offline
static lv_obj_t* s_spFam[SP_FAMS], *s_spFamLbl[SP_FAMS], *s_spFamCnt[SP_FAMS], *s_spFamEdge[SP_FAMS];
static lv_obj_t* s_spPill[SP_SLOTS], *s_spPillName[SP_SLOTS], *s_spPillSub[SP_SLOTS],
               *s_spPillState[SP_SLOTS], *s_spPillEdge[SP_SLOTS];
static lv_obj_t* s_spPager;
static uint8_t   s_spFamSel = 4;          // soccer has the most; a fine default
static uint8_t   s_spPage;
static uint32_t  s_spTryAt;               // last catalog attempt
static uint8_t   s_spTries;               // attempts this boot (retry, don't latch)

static const char* kFamName[SP_FAMS] =
  { "FOOTBALL", "BASKETBALL", "HOCKEY", "BASEBALL", "SOCCER", "TENNIS", "GOLF", "RACING" };

/** Proxy families plus the UI-only split of `other` by slug — a five-entry
 *  static map, not registry knowledge. Unknown future slugs land in RACING,
 *  which is wrong for them and right for not hiding them entirely. */
static uint8_t famOf(const char* slug, const char* family) {
  if (strcmp(family, "football") == 0)   return 0;
  if (strcmp(family, "basketball") == 0) return 1;
  if (strcmp(family, "hockey") == 0)     return 2;
  if (strcmp(family, "baseball") == 0)   return 3;
  if (strcmp(family, "soccer") == 0)     return 4;
  if (strcmp(slug, "atp") == 0 || strcmp(slug, "wta") == 0)   return 5;
  if (strcmp(slug, "pga") == 0 || strcmp(slug, "lpga") == 0)  return 6;
  return 7;
}

/** The working catalog: the fetched one, or a degraded set synthesised from
 *  (stored CSV ∪ tonight's board) so the pane still works offline. */
static uint8_t spCatalog(CatEntry* out, uint8_t cap) {
  if (g_catalogLoaded) {
    const uint8_t n = g_catalogCount < cap ? g_catalogCount : cap;
    memcpy(out, g_catalog, n * sizeof(CatEntry));
    return n;
  }
  uint8_t n = 0;
  auto add = [&](const char* slug) {
    if (!slug[0] || n >= cap) return;
    for (uint8_t i = 0; i < n; i++) if (strcmp(out[i].slug, slug) == 0) return;
    CatEntry& c = out[n++];
    memset(&c, 0, sizeof c);
    strncpy(c.slug, slug, sizeof c.slug - 1);
    size_t j = 0;
    for (; slug[j] && j < sizeof c.label - 1; j++) c.label[j] = (char)toupper((unsigned char)slug[j]);
    c.label[j] = '\0';
  };
  for (uint8_t i = 0; i < g_leagueCount; i++) add(g_leagues[i].slug);
  // The stored CSV too — an enabled league with no game tonight must be
  // visible, or the user cannot turn it off.
  const char* csv = g_set.leagues.c_str();
  char slug[10]; size_t k = 0;
  for (size_t i = 0; ; i++) {
    const char ch = csv[i];
    if (ch == ',' || ch == '\0') {
      slug[k] = '\0'; if (k) add(slug); k = 0;
      if (!ch) break;
    } else if (k < sizeof slug - 1) slug[k++] = ch;
  }
  return n;
}

/** Leagues kept on by a favourite — the proxy re-merges them regardless, so
 *  the pane must refuse to pretend they can be turned off. */
static uint8_t spAutoList(char out[][10], uint8_t cap) {
  uint8_t n = 0;
  char lg[10], id[14];
  for (uint8_t i = 0; i < favCount() && n < cap; i++) {
    if (!favAt(i, lg, sizeof lg, id, sizeof id)) continue;
    bool dup = false;
    for (uint8_t k = 0; k < n; k++) if (strcmp(out[k], lg) == 0) { dup = true; break; }
    if (!dup) { strncpy(out[n], lg, 10); out[n][9] = '\0'; n++; }
  }
  return n;
}

static bool spSelHas(const char* slug) {
  const char* csv = g_set.leagues.c_str();
  const size_t sl = strlen(slug);
  for (const char* p2 = strstr(csv, slug); p2; p2 = strstr(p2 + 1, slug)) {
    const bool a = (p2 == csv) || p2[-1] == ',';
    const bool b = p2[sl] == ',' || p2[sl] == '\0';
    if (a && b) return true;
  }
  return false;
}

static void spSelToggle(const char* slug, bool on) {
  String next;
  const char* csv = g_set.leagues.c_str();
  char cur[10]; size_t k = 0;
  for (size_t i = 0; ; i++) {
    const char ch = csv[i];
    if (ch == ',' || ch == '\0') {
      cur[k] = '\0';
      if (k && strcmp(cur, slug) != 0) {
        if (next.length()) next += ',';
        next += cur;
      }
      k = 0;
      if (!ch) break;
    } else if (k < sizeof cur - 1) cur[k++] = ch;
  }
  if (on) {
    if (next.length()) next += ',';
    next += slug;
  }
  g_set.leagues = next;
  s_leaguesDirty = true;
  markDirty();
}

static void renderSports();

static void onSpFam(lv_event_t* e) {
  s_spFamSel = (uint8_t)(intptr_t)lv_event_get_user_data(e);
  s_spPage = 0;
  renderSports();
}
static void onSpPager(lv_event_t*) { s_spPage ^= 1; renderSports(); }

static void onSpPill(lv_event_t* e) {
  const int slot = (int)(intptr_t)lv_event_get_user_data(e);
  static CatEntry cat[CAT_MAX];
  const uint8_t n = spCatalog(cat, CAT_MAX);
  char autoLg[MAX_LEAGUES][10];
  const uint8_t nAuto = spAutoList(autoLg, MAX_LEAGUES);

  uint8_t idx = 0;
  for (uint8_t i = 0; i < n; i++) {
    if (famOf(cat[i].slug, cat[i].family) != s_spFamSel) continue;
    if (idx / SP_SLOTS == s_spPage && (idx % SP_SLOTS) == slot) {
      bool isAuto = false;
      for (uint8_t a = 0; a < nAuto; a++)
        if (strcmp(autoLg[a], cat[i].slug) == 0) { isAuto = true; break; }
      if (isAuto) {
        char msg[64];
        snprintf(msg, sizeof msg, "%s STAYS ON WHILE A FAVOURITE PLAYS THERE", cat[i].label);
        lv_label_set_text(s_spMsg, msg);
        lv_obj_set_style_text_color(s_spMsg, C_INK2, 0);
        return;
      }
      const bool on = spSelHas(cat[i].slug);
      if (!on) {
        uint8_t total = nAuto;
        for (uint8_t c2 = 0; c2 < n; c2++)
          if (spSelHas(cat[c2].slug)) {
            bool dup = false;
            for (uint8_t a = 0; a < nAuto; a++)
              if (strcmp(autoLg[a], cat[c2].slug) == 0) { dup = true; break; }
            if (!dup) total++;
          }
        if (total >= MAX_LEAGUES) {
          lv_label_set_text(s_spMsg, "AT LIMIT - TURN ONE OFF TO ADD ANOTHER");
          lv_obj_set_style_text_color(s_spMsg, C_WARN, 0);
          return;
        }
      }
      spSelToggle(cat[i].slug, !on);
      renderSports();
      return;
    }
    idx++;
  }
}

static void buildSports(lv_obj_t* p) {
  label(p, 24, 14, "Your favourite teams always show. Pick the leagues that fill the rest.",
        C_INK2, F_BODY);
  s_spMeterLbl = label(p, 560, 40, "", C_INK2, F_NUM);
  for (uint8_t i = 0; i < MAX_LEAGUES; i++) {
    s_spTick[i] = lv_obj_create(p);
    lv_obj_remove_style_all(s_spTick[i]);
    lv_obj_set_size(s_spTick[i], 8, 10);
    lv_obj_set_pos(s_spTick[i], 630 + i * 11, 42);
    lv_obj_set_style_bg_color(s_spTick[i], C_EDGE, 0);
    lv_obj_set_style_bg_opa(s_spTick[i], LV_OPA_COVER, 0);
  }
  s_spMsg = label(p, 24, 40, "", kStateInk[GS_LIVE].ink3, F_MICRO);
  lv_obj_set_style_text_letter_space(s_spMsg, 1, 0);

  for (uint8_t f = 0; f < SP_FAMS; f++) {
    const int y = 66 + f * 40;
    s_spFam[f] = lv_obj_create(p);
    lv_obj_remove_style_all(s_spFam[f]);
    lv_obj_set_size(s_spFam[f], 140, 34);
    lv_obj_set_pos(s_spFam[f], 16, y);
    lv_obj_set_style_radius(s_spFam[f], R_MD, 0);
    lv_obj_set_style_bg_color(s_spFam[f], C_SURF_3, 0);
    lv_obj_set_style_bg_opa(s_spFam[f], LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_spFam[f], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_spFam[f], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_spFam[f], onSpFam, LV_EVENT_CLICKED, (void*)(intptr_t)f);
    s_spFamEdge[f] = lv_obj_create(p);
    lv_obj_remove_style_all(s_spFamEdge[f]);
    lv_obj_set_size(s_spFamEdge[f], 3, 34);
    lv_obj_set_pos(s_spFamEdge[f], 13, y);
    lv_obj_set_style_bg_color(s_spFamEdge[f], C_INK, 0);   // selection is INK_1 (PLAN §6)
    lv_obj_set_style_bg_opa(s_spFamEdge[f], LV_OPA_COVER, 0);
    lv_obj_add_flag(s_spFamEdge[f], LV_OBJ_FLAG_HIDDEN);
    s_spFamLbl[f] = label(p, 28, y + 10, kFamName[f], kStateInk[GS_LIVE].ink3, F_MICRO);
    lv_obj_set_style_text_letter_space(s_spFamLbl[f], 1, 0);
    s_spFamCnt[f] = label(p, 128, y + 10, "", kStateInk[GS_LIVE].ink3, F_MICRO);
  }

  for (uint8_t i = 0; i < SP_SLOTS; i++) {
    const int x = 176 + (i % 2) * 288, y = 66 + (i / 2) * 62;
    s_spPill[i] = lv_obj_create(p);
    lv_obj_remove_style_all(s_spPill[i]);
    lv_obj_set_size(s_spPill[i], 276, 52);
    lv_obj_set_pos(s_spPill[i], x, y);
    lv_obj_set_style_radius(s_spPill[i], R_MD, 0);
    lv_obj_set_style_bg_opa(s_spPill[i], LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_spPill[i], 1, 0);
    lv_obj_clear_flag(s_spPill[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_spPill[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_spPill[i], onSpPill, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    s_spPillEdge[i] = lv_obj_create(s_spPill[i]);
    lv_obj_remove_style_all(s_spPillEdge[i]);
    lv_obj_set_size(s_spPillEdge[i], 3, 52);
    lv_obj_set_pos(s_spPillEdge[i], 0, 0);
    lv_obj_set_style_bg_color(s_spPillEdge[i], C_INK, 0);  // selection is INK_1 (PLAN §6)
    lv_obj_set_style_bg_opa(s_spPillEdge[i], LV_OPA_COVER, 0);
    s_spPillName[i] = label(s_spPill[i], 14, 8, "", C_INK, F_BODY);
    s_spPillSub[i] = label(s_spPill[i], 14, 30, "", C_INK3, F_MICRO);
    lv_obj_set_style_text_letter_space(s_spPillSub[i], 1, 0);
    s_spPillState[i] = label(s_spPill[i], 0, 18, "", C_INK, F_MICRO);
    lv_obj_set_width(s_spPillState[i], 56);
    lv_obj_set_style_text_align(s_spPillState[i], LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_x(s_spPillState[i], 276 - 14 - 56);
  }

  s_spPager = lv_obj_create(p);
  lv_obj_remove_style_all(s_spPager);
  lv_obj_set_size(s_spPager, 120, 30);
  lv_obj_set_pos(s_spPager, 176, 66 + 4 * 62);
  lv_obj_set_style_radius(s_spPager, R_MD, 0);
  lv_obj_set_style_bg_color(s_spPager, C_FROST_2, 0);
  lv_obj_set_style_bg_opa(s_spPager, LV_OPA_COVER, 0);
  lv_obj_add_flag(s_spPager, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(s_spPager, onSpPager, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* pgl = label(s_spPager, 0, 0, "MORE >", C_INK2, F_MICRO);
  lv_obj_center(pgl);
  // kStateInk[GS_LIVE].ink3 — the review measured the only statement of the
  // save model at 4.01:1 on this pane; the solved tier clears 4.55.
  label(p, 560, 66 + 4 * 62 + 8, "saved when you leave", kStateInk[GS_LIVE].ink3, F_MICRO);
}

static void renderSports() {
  static CatEntry cat[CAT_MAX];
  const uint8_t n = spCatalog(cat, CAT_MAX);
  char autoLg[MAX_LEAGUES][10];
  const uint8_t nAuto = spAutoList(autoLg, MAX_LEAGUES);
  auto isAuto = [&](const char* slug) {
    for (uint8_t a = 0; a < nAuto; a++) if (strcmp(autoLg[a], slug) == 0) return true;
    return false;
  };

  uint8_t nSel = 0;
  for (uint8_t i = 0; i < n; i++)
    if (spSelHas(cat[i].slug) && !isAuto(cat[i].slug)) nSel++;
  const uint8_t total = (uint8_t)(nAuto + nSel);
  char m[12];
  snprintf(m, sizeof m, "%u / %u", (unsigned)total, (unsigned)MAX_LEAGUES);
  lv_label_set_text(s_spMeterLbl, m);
  lv_obj_set_style_text_color(s_spMeterLbl, total >= MAX_LEAGUES ? C_WARN : C_INK2, 0);
  for (uint8_t i = 0; i < MAX_LEAGUES; i++) {
    lv_color_t c = C_EDGE;
    if (i < nAuto) c = C_EDGE_HI;                      // spoken for
    else if (i < total) c = total >= MAX_LEAGUES ? S_ALERT : C_INK3;   // structure, not liveness
    lv_obj_set_style_bg_color(s_spTick[i], c, 0);
  }
  if (total >= MAX_LEAGUES) {
    lv_label_set_text(s_spMsg, "AT LIMIT - TURN ONE OFF TO ADD ANOTHER");
    lv_obj_set_style_text_color(s_spMsg, C_WARN, 0);
  } else if (!g_catalogLoaded) {
    // Honest states: keep saying LOADING while retries are still young —
    // "could not reach" is only earned after ~3 real attempts have failed.
    lv_label_set_text(s_spMsg, (g_catalogInFlight || s_spTries < 3)
        ? "LOADING THE CATALOG..."
        : "COULD NOT REACH THE PROXY - SHOWING WHAT'S ON THE BOARD");
    lv_obj_set_style_text_color(s_spMsg, C_INK3, 0);
  } else if (!g_set.leagues.length()) {
    lv_label_set_text(s_spMsg, "USING THE DEFAULT SET - ANY CHANGE MAKES IT YOURS");
    lv_obj_set_style_text_color(s_spMsg, C_INK3, 0);
  } else {
    lv_label_set_text(s_spMsg, "");
  }

  for (uint8_t f = 0; f < SP_FAMS; f++) {
    uint8_t cnt = 0, present = 0;
    for (uint8_t i = 0; i < n; i++) {
      if (famOf(cat[i].slug, cat[i].family) != f) continue;
      present++;
      if (spSelHas(cat[i].slug) || isAuto(cat[i].slug)) cnt++;
    }
    const bool sel = (f == s_spFamSel);
    lv_obj_set_style_bg_opa(s_spFam[f], sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    sel ? lv_obj_clear_flag(s_spFamEdge[f], LV_OBJ_FLAG_HIDDEN)
        : lv_obj_add_flag(s_spFamEdge[f], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_color(s_spFamLbl[f], sel ? C_INK : kStateInk[GS_LIVE].ink3, 0);
    char cb[6];
    snprintf(cb, sizeof cb, "%u", (unsigned)cnt);
    lv_label_set_text(s_spFamCnt[f], present ? cb : "-");
    lv_obj_set_style_text_color(s_spFamCnt[f], cnt ? C_INK : kStateInk[GS_LIVE].ink3, 0);
  }

  uint8_t famTotal = 0;
  for (uint8_t i = 0; i < n; i++)
    if (famOf(cat[i].slug, cat[i].family) == s_spFamSel) famTotal++;
  const bool paged = famTotal > SP_SLOTS;
  if (!paged) s_spPage = 0;
  paged ? lv_obj_clear_flag(s_spPager, LV_OBJ_FLAG_HIDDEN)
        : lv_obj_add_flag(s_spPager, LV_OBJ_FLAG_HIDDEN);

  uint8_t idx = 0, slot = 0;
  for (uint8_t i = 0; i < n && slot < SP_SLOTS; i++) {
    if (famOf(cat[i].slug, cat[i].family) != s_spFamSel) continue;
    if (idx++ / SP_SLOTS != s_spPage) continue;
    const uint8_t sl = slot++;
    lv_obj_clear_flag(s_spPill[sl], LV_OBJ_FLAG_HIDDEN);
    const bool au = isAuto(cat[i].slug);
    const bool on = au || spSelHas(cat[i].slug);
    lv_label_set_text(s_spPillName[sl], cat[i].label);
    uint8_t liveN = 0;
    for (uint8_t k = 0; k < g_leagueCount; k++)
      if (strcmp(g_leagues[k].slug, cat[i].slug) == 0) liveN = g_leagues[k].live;
    char sub[26];
    if (au)         snprintf(sub, sizeof sub, "KEPT ON BY A FAVOURITE");
    else if (liveN) snprintf(sub, sizeof sub, "%u LIVE NOW", (unsigned)liveN);
    else            snprintf(sub, sizeof sub, "%s", cat[i].slug);
    lv_label_set_text(s_spPillSub[sl], sub);
    lv_obj_set_style_text_color(s_spPillSub[sl],
        (!au && liveN) ? C_LIVE_TX : C_INK3, 0);

    lv_label_set_text(s_spPillState[sl], au ? "AUTO" : (on ? "ON" : "-"));
    lv_obj_set_style_text_color(s_spPillState[sl],
        au ? C_INK3 : (on ? C_INK : C_INK3), 0);
    lv_obj_set_style_bg_color(s_spPill[sl], on ? C_SURF_2 : C_PLATE, 0);
    lv_obj_set_style_border_color(s_spPill[sl], on ? C_EDGE_HI : C_EDGE, 0);
    lv_obj_set_style_border_opa(s_spPill[sl], LV_OPA_COVER, 0);
    on ? lv_obj_clear_flag(s_spPillEdge[sl], LV_OBJ_FLAG_HIDDEN)
       : lv_obj_add_flag(s_spPillEdge[sl], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_color(s_spPillName[sl], on ? C_INK : C_INK2, 0);
  }
  for (; slot < SP_SLOTS; slot++) lv_obj_add_flag(s_spPill[slot], LV_OBJ_FLAG_HIDDEN);
}

static void spKick() {
  // Retry, never latch: the old one-shot flag meant a single failed start —
  // settings opened before Wi-Fi finished associating after a reboot was
  // enough — wore "COULD NOT REACH THE PROXY" for the whole session while
  // the proxy sat healthy. Attempts are 3 s apart, capped at 10 per boot.
  if (!g_catalogLoaded && !g_catalogInFlight && s_spTries < 10 &&
      (s_spTries == 0 || millis() - s_spTryAt > 3000)) {
    s_spTryAt = millis();
    s_spTries++;
    apiCatalogStart();
  }
  renderSports();
}

static void onTab(lv_event_t* e) {
  s_tab = (uint8_t)(intptr_t)lv_event_get_user_data(e);
  if (s_tab == 1) spKick();      // first open fetches the catalog
  uiSettingsRender();
}
static void onDone(lv_event_t*) { uiSettingsClose(); }

static void onDensity(lv_event_t* e) {
  g_set.density = (uint8_t)(intptr_t)lv_event_get_user_data(e);
  markDirty();
  uiSettingsRender();
}
static void onAlerts(lv_event_t*) { g_set.alertsOn = !g_set.alertsOn; markDirty(); uiSettingsRender(); }
static void onFocus(lv_event_t*)  { g_set.focusOn  = !g_set.focusOn;  markDirty(); uiSettingsRender(); }
static void onQuiet(lv_event_t*)  { g_set.quietOn  = !g_set.quietOn;  markDirty(); uiSettingsRender(); }

static void onClock(lv_event_t* e) {
  g_set.clock24 = (bool)(intptr_t)lv_event_get_user_data(e);
  markDirty();
  // No need to force a repaint: tickClock() rewrites the bar every second and
  // the idle screen with it, so the new format lands on its own.
  uiSettingsRender();
}

static void onFavPage(lv_event_t*) {
  const uint8_t n = favCount();
  const uint8_t pages = n ? (n + FAV_ROWS - 1) / FAV_ROWS : 1;
  s_favPage = (uint8_t)((s_favPage + 1) % pages);
  uiSettingsRender();
}

static void onFavUp(lv_event_t* e) {
  const uint8_t i = s_favPage * FAV_ROWS + (uint8_t)(intptr_t)lv_event_get_user_data(e);
  if (favMoveUp(i)) { markDirty(); uiSettingsRender(); }
}
static void onFavDrop(lv_event_t* e) {
  const uint8_t i = s_favPage * FAV_ROWS + (uint8_t)(intptr_t)lv_event_get_user_data(e);
  if (favRemove(i)) { markDirty(); uiSettingsRender(); }
}

static void tzRender();

static void onTzOpen(lv_event_t*) {
  s_tzPage = 0;
  lv_obj_clear_flag(s_tzView, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(s_tzView);
  tzRender();
}
static void onTzClose(lv_event_t*) {
  lv_obj_add_flag(s_tzView, LV_OBJ_FLAG_HIDDEN);
  uiSettingsRender();
}
static void onTzPage(lv_event_t*) {
  const uint8_t pages = (kTimeZoneCount + TZ_PER - 1) / TZ_PER;
  s_tzPage = (uint8_t)((s_tzPage + 1) % pages);
  tzRender();
}
static void onTzPick(lv_event_t* e) {
  const uint8_t i = s_tzPage * TZ_PER + (uint8_t)(intptr_t)lv_event_get_user_data(e);
  if (i >= kTimeZoneCount) return;
  tzApply(kTimeZones[i].iana);
  markDirty();
  lv_obj_add_flag(s_tzView, LV_OBJ_FLAG_HIDDEN);
  uiSettingsRender();
}

static void tzRender() {
  // tzForProxy(), not g_set.tzIana directly. The raw field is empty until the
  // user has picked one, so on a fresh panel `cur` was -1 and NONE of the
  // fourteen buttons marked the zone actually in force — the summary row at
  // :1009 already resolved it through tzForProxy() and the picker disagreed
  // with it.
  const int8_t cur = tzIndexOf(tzForProxy());
  for (uint8_t k = 0; k < TZ_PER; k++) {
    const uint8_t i = s_tzPage * TZ_PER + k;
    if (i >= kTimeZoneCount) { lv_obj_add_flag(s_tzBtn[k], LV_OBJ_FLAG_HIDDEN); continue; }
    lv_obj_clear_flag(s_tzBtn[k], LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_tzBtnLbl[k], kTimeZones[i].label);
    const bool on = ((int8_t)i == cur);
    lv_obj_set_style_bg_color(s_tzBtn[k], on ? C_EDGE_HI : C_FROST_2, 0);
    lv_obj_set_style_text_color(s_tzBtnLbl[k], on ? C_INK : C_INK2, 0);
    // Not fill alone. A single filled row among fourteen is easy to miss and
    // impossible to see if you cannot separate the two greys; the mark says it
    // in a second channel.
    on ? lv_obj_clear_flag(s_tzTick[k], LV_OBJ_FLAG_HIDDEN)
       : lv_obj_add_flag(s_tzTick[k], LV_OBJ_FLAG_HIDDEN);
  }
  char b[24];
  const uint8_t pages = (kTimeZoneCount + TZ_PER - 1) / TZ_PER;
  snprintf(b, sizeof b, "%u / %u", s_tzPage + 1, pages);
  lv_label_set_text(s_tzPageLbl, b);
}

static void onProxyTest(lv_event_t*) {
  // The single highest-value diagnostic available without a laptop: it settles
  // "is it the proxy or the network" on the spot.
  lv_label_set_text(s_testLbl, "testing");
  lv_refr_now(nullptr);
  uint16_t ms = 0;
  const int code = netProbeProxy(&ms);
  char b[32];
  if (code > 0) snprintf(b, sizeof b, "%d  %u ms", code, ms);
  else          snprintf(b, sizeof b, "unreachable");
  lv_label_set_text(s_testLbl, b);
}

static void onClearToken(lv_event_t*) { g_set.token = ""; markDirty(); uiSettingsRender(); }
static void onClearPass(lv_event_t*)  { g_set.panelPass = ""; markDirty(); uiSettingsRender(); }
static void onReboot(lv_event_t*)     { flushNow(); ESP.restart(); }

// ── build ──────────────────────────────────────────────────────────────────
void uiSettingsInit(lv_obj_t* parent) {
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

  lv_obj_t* bar = glassPanel(s_root, 0, 0, SCR_W, 48, 0);
  label(bar, 20, 15, "SETTINGS", C_INK, F_ABBR);

  // Three panes do not justify a 176 px rail, so the selector is a segment.
  static const char* kTab[PANE_COUNT] = { "BOARD", "SPORTS", "TEAMS", "NETWORK", "SYSTEM" };
  for (uint8_t i = 0; i < PANE_COUNT; i++) {
    s_seg[i] = lv_btn_create(bar);
    // Five tabs: 88 wide on a 92 pitch from x=172 (was 104/108 from 196).
    lv_obj_set_size(s_seg[i], 88, 36);
    lv_obj_set_pos(s_seg[i], 172 + i * 92, 6);
    lv_obj_set_style_radius(s_seg[i], R_MD, 0);
    lv_obj_set_style_border_width(s_seg[i], 0, 0);
    lv_obj_add_event_cb(s_seg[i], onTab, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    s_segLbl[i] = lv_label_create(s_seg[i]);
    lv_label_set_text(s_segLbl[i], kTab[i]);
    lv_obj_set_style_text_font(s_segLbl[i], F_MICRO, 0);
    lv_obj_center(s_segLbl[i]);
  }

  s_dirtyDot = lv_obj_create(bar);
  lv_obj_remove_style_all(s_dirtyDot);
  lv_obj_set_size(s_dirtyDot, 8, 8);
  lv_obj_set_pos(s_dirtyDot, 676, 20);
  lv_obj_set_style_radius(s_dirtyDot, 4, 0);
  lv_obj_set_style_bg_color(s_dirtyDot, C_INK2, 0);
  lv_obj_set_style_bg_opa(s_dirtyDot, LV_OPA_COVER, 0);
  lv_obj_add_flag(s_dirtyDot, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* done = lv_btn_create(bar);
  lv_obj_set_size(done, 88, 36);
  lv_obj_set_pos(done, SCR_W - 20 - 88, 6);
  lv_obj_set_style_bg_color(done, C_EDGE, 0);
  lv_obj_set_style_radius(done, R_MD, 0);
  lv_obj_set_style_border_width(done, 0, 0);
  lv_obj_add_event_cb(done, onDone, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* dl = lv_label_create(done);
  lv_label_set_text(dl, "DONE");
  lv_obj_set_style_text_font(dl, F_MICRO, 0);
  lv_obj_set_style_text_color(dl, C_INK, 0);
  lv_obj_center(dl);

  for (uint8_t i = 0; i < PANE_COUNT; i++) {
    s_pane[i] = glassPanel(s_root, 16, 60, 768, 404, 14);
    lv_obj_add_flag(s_pane[i], LV_OBJ_FLAG_HIDDEN);
  }

  // ── BOARD ────────────────────────────────────────────────────────────────
  {
    lv_obj_t* p = s_pane[0];
    lv_obj_t* r = row(p, 0, "Density", nullptr);
    static const char* kDen[DEN_COUNT] = { "ROOMY", "STD", "DENSE", "AUTO" };
    for (uint8_t i = 0; i < DEN_COUNT; i++) {
      s_denSeg[i] = lv_btn_create(r);
      lv_obj_set_size(s_denSeg[i], 82, 38);
      lv_obj_set_pos(s_denSeg[i], 736 - 8 - (DEN_COUNT - i) * 86, (ROW_H - 38) / 2);
      lv_obj_set_style_radius(s_denSeg[i], R_MD, 0);
      lv_obj_set_style_border_width(s_denSeg[i], 0, 0);
      lv_obj_add_event_cb(s_denSeg[i], onDensity, LV_EVENT_CLICKED, (void*)(intptr_t)i);
      lv_obj_t* l = lv_label_create(s_denSeg[i]);
      lv_label_set_text(l, kDen[i]);
      lv_obj_set_style_text_font(l, F_MICRO, 0);
      lv_obj_center(l);
    }

    lv_obj_t* cr = row(p, 1, "Clock", nullptr);
    static const char* kClk[2] = { "12H", "24H" };
    for (uint8_t i = 0; i < 2; i++) {
      s_clkSeg[i] = lv_btn_create(cr);
      lv_obj_set_size(s_clkSeg[i], 82, 38);
      lv_obj_set_pos(s_clkSeg[i], 736 - 8 - (2 - i) * 86, (ROW_H - 38) / 2);
      lv_obj_set_style_radius(s_clkSeg[i], R_MD, 0);
      lv_obj_set_style_border_width(s_clkSeg[i], 0, 0);
      lv_obj_add_event_cb(s_clkSeg[i], onClock, LV_EVENT_CLICKED, (void*)(intptr_t)i);
      lv_obj_t* l = lv_label_create(s_clkSeg[i]);
      lv_label_set_text(l, kClk[i]);
      lv_obj_set_style_text_font(l, F_MICRO, 0);
      lv_obj_center(l);
    }

    s_swAlerts = switchAt(row(p, 2, "Score alerts", onAlerts), g_set.alertsOn);
    s_swFocus  = switchAt(row(p, 3, "Open tense games automatically", onFocus), g_set.focusOn);
    s_swQuiet  = switchAt(row(p, 4, "Quiet hours", onQuiet), g_set.quietOn);

  }

  // ── TEAMS ────────────────────────────────────────────────────────────────
  {
    buildSports(s_pane[1]);
  }
  {
    lv_obj_t* p = s_pane[2];
    lv_obj_t* fr = row(p, 0, "Your teams", nullptr);
    s_favCount = label(fr, 160, (ROW_H - 13) / 2, "", C_INK3, F_NUM);
    lv_obj_t* pageBtn = lv_btn_create(fr);
    lv_obj_set_size(pageBtn, 60, 38);
    lv_obj_set_pos(pageBtn, 736 - 8 - 60, (ROW_H - 38) / 2);
    lv_obj_set_style_radius(pageBtn, R_MD, 0);
    lv_obj_set_style_border_width(pageBtn, 0, 0);
    lv_obj_set_style_bg_color(pageBtn, C_EDGE, 0);
    lv_obj_add_event_cb(pageBtn, onFavPage, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* pl = lv_label_create(pageBtn);
    lv_label_set_text(pl, "MORE");
    lv_obj_set_style_text_font(pl, F_MICRO, 0);
    lv_obj_center(pl);

    for (uint8_t i = 0; i < FAV_ROWS; i++) {
      s_favRow[i] = lv_obj_create(p);
      lv_obj_remove_style_all(s_favRow[i]);
      lv_obj_set_size(s_favRow[i], 736, 36);
      lv_obj_set_pos(s_favRow[i], 16, 16 + ROW_PITCH + i * 40);
      lv_obj_set_style_bg_opa(s_favRow[i], LV_OPA_TRANSP, 0);
      lv_obj_clear_flag(s_favRow[i], LV_OBJ_FLAG_SCROLLABLE);

      s_favSwatch[i] = lv_obj_create(s_favRow[i]);
      lv_obj_remove_style_all(s_favSwatch[i]);
      lv_obj_set_size(s_favSwatch[i], 5, 24);
      lv_obj_set_pos(s_favSwatch[i], 8, 6);
      lv_obj_set_style_radius(s_favSwatch[i], 2, 0);
      lv_obj_set_style_bg_opa(s_favSwatch[i], LV_OPA_COVER, 0);

      s_favLbl[i] = label(s_favRow[i], 24, 9, "", C_INK, F_BODY);

      lv_obj_t* up = lv_btn_create(s_favRow[i]);
      lv_obj_set_size(up, 44, 32);
      lv_obj_set_pos(up, 736 - 8 - 96, 2);
      lv_obj_set_style_radius(up, 6, 0);
      lv_obj_set_style_border_width(up, 0, 0);
      lv_obj_set_style_bg_color(up, C_EDGE, 0);
      lv_obj_add_event_cb(up, onFavUp, LV_EVENT_CLICKED, (void*)(intptr_t)i);
      lv_obj_t* ul = lv_label_create(up);
      lv_label_set_text(ul, "UP");
      lv_obj_set_style_text_font(ul, F_MICRO, 0);
      lv_obj_center(ul);

      lv_obj_t* rm = lv_btn_create(s_favRow[i]);
      lv_obj_set_size(rm, 44, 32);
      lv_obj_set_pos(rm, 736 - 8 - 44, 2);
      lv_obj_set_style_radius(rm, 6, 0);
      lv_obj_set_style_border_width(rm, 0, 0);
      lv_obj_set_style_bg_color(rm, C_EDGE, 0);
      lv_obj_add_event_cb(rm, onFavDrop, LV_EVENT_CLICKED, (void*)(intptr_t)i);
      lv_obj_t* rl = lv_label_create(rm);
      lv_label_set_text(rl, "X");
      lv_obj_set_style_text_font(rl, F_MICRO, 0);
      lv_obj_center(rl);
    }
    label(p, 16, 380, "Add and search teams in the browser  -  this list only reorders",
          C_INK3, F_MICRO);
  }

  // ── NETWORK ──────────────────────────────────────────────────────────────
  {
    lv_obj_t* p = s_pane[3];
    static const char* kRow[5] = { "Wi-Fi", "Proxy", "Proxy token", "Panel password", "Time zone" };
    for (uint8_t i = 0; i < 5; i++) {
      lv_obj_t* r = row(p, i, kRow[i], i == 4 ? onTzOpen : nullptr);
      s_netVal[i] = label(r, 220, (ROW_H - 15) / 2, "", C_INK2, F_BODY);
      lv_obj_set_width(s_netVal[i], 380);
      lv_label_set_long_mode(s_netVal[i], LV_LABEL_LONG_DOT);

      if (i == 1) {
        lv_obj_t* t = lv_btn_create(r);
        lv_obj_set_size(t, 76, 38);
        lv_obj_set_pos(t, 736 - 8 - 76, (ROW_H - 38) / 2);
        lv_obj_set_style_radius(t, R_MD, 0);
        lv_obj_set_style_border_width(t, 0, 0);
        lv_obj_set_style_bg_color(t, C_EDGE, 0);
        lv_obj_add_event_cb(t, onProxyTest, LV_EVENT_CLICKED, nullptr);
        lv_obj_t* l = lv_label_create(t);
        lv_label_set_text(l, "TEST");
        lv_obj_set_style_text_font(l, F_MICRO, 0);
        lv_obj_center(l);
        s_testLbl = label(r, 736 - 8 - 76 - 130, (ROW_H - 13) / 2, "", C_INK3, F_NUM);
        lv_obj_set_width(s_testLbl, 122);
        lv_obj_set_style_text_align(s_testLbl, LV_TEXT_ALIGN_RIGHT, 0);
      }
      if (i == 2 || i == 3) {   // secrets only — NOT the timezone row
        // The panel is the password-recovery path on purpose: physical access
        // is already total access, and a forgotten password must not brick
        // the portal.
        lv_obj_t* cbtn = lv_btn_create(r);
        lv_obj_set_size(cbtn, 88, 38);
        lv_obj_set_pos(cbtn, 736 - 8 - 88, (ROW_H - 38) / 2);
        lv_obj_set_style_radius(cbtn, R_MD, 0);
        lv_obj_set_style_border_width(cbtn, 0, 0);
        lv_obj_set_style_bg_color(cbtn, C_EDGE, 0);
        lv_obj_add_event_cb(cbtn, i == 2 ? onClearToken : onClearPass,
                            LV_EVENT_CLICKED, nullptr);
        lv_obj_t* l = lv_label_create(cbtn);
        lv_label_set_text(l, "CLEAR");
        lv_obj_set_style_text_font(l, F_MICRO, 0);
        lv_obj_center(l);
      }
    }
    label(p, 16, 380, "Edit the proxy URL and add teams in the browser", C_INK3, F_MICRO);

    // ── timezone sub-view ──────────────────────────────────────────────────
    s_tzView = glassPanel(s_root, 16, 60, 768, 404, 14);
    lv_obj_add_flag(s_tzView, LV_OBJ_FLAG_HIDDEN);
    label(s_tzView, 20, 16, "TIME ZONE", C_INK3, F_MICRO);

    lv_obj_t* back = lv_btn_create(s_tzView);
    lv_obj_set_size(back, 116, 36);
    lv_obj_set_pos(back, 768 - 20 - 116, 10);
    lv_obj_set_style_radius(back, R_MD, 0);
    lv_obj_set_style_border_width(back, 0, 0);
    lv_obj_set_style_bg_color(back, C_EDGE, 0);
    lv_obj_add_event_cb(back, onTzClose, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* bl = lv_label_create(back);
    // "< NETWORK", not a second DONE. Two identical DONE buttons sat 64 px
    // apart doing opposite things: this one returns to the Network pane, the
    // one behind it closes settings entirely.
    lv_label_set_text(bl, "< NETWORK");
    lv_obj_set_style_text_font(bl, F_MICRO, 0);
    lv_obj_center(bl);

    for (uint8_t k = 0; k < TZ_PER; k++) {
      const int col = k % TZ_COLS, rw = k / TZ_COLS;
      s_tzBtn[k] = lv_btn_create(s_tzView);
      lv_obj_set_size(s_tzBtn[k], 356, 38);
      lv_obj_set_pos(s_tzBtn[k], 20 + col * 372, 52 + rw * 42);
      lv_obj_set_style_radius(s_tzBtn[k], R_MD, 0);
      lv_obj_set_style_border_width(s_tzBtn[k], 0, 0);
      lv_obj_add_event_cb(s_tzBtn[k], onTzPick, LV_EVENT_CLICKED, (void*)(intptr_t)k);
      s_tzBtnLbl[k] = lv_label_create(s_tzBtn[k]);
      lv_obj_set_style_text_font(s_tzBtnLbl[k], F_BODY, 0);   // city names
      lv_label_set_text(s_tzBtnLbl[k], "");
      lv_obj_align(s_tzBtnLbl[k], LV_ALIGN_LEFT_MID, 12, 0);
      s_tzTick[k] = lv_label_create(s_tzBtn[k]);
      lv_obj_set_style_text_font(s_tzTick[k], F_BODY, 0);
      lv_obj_set_style_text_color(s_tzTick[k], C_INK, 0);
      lv_label_set_text(s_tzTick[k], "IN USE");
      lv_obj_set_style_text_font(s_tzTick[k], F_MICRO, 0);
      lv_obj_align(s_tzTick[k], LV_ALIGN_RIGHT_MID, -12, 0);
      lv_obj_add_flag(s_tzTick[k], LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_t* more = lv_btn_create(s_tzView);
    lv_obj_set_size(more, 120, 40);
    lv_obj_set_pos(more, 20, 52 + TZ_ROWS * 42 + 6);
    lv_obj_set_style_radius(more, R_MD, 0);
    lv_obj_set_style_border_width(more, 0, 0);
    lv_obj_set_style_bg_color(more, C_EDGE, 0);
    lv_obj_add_event_cb(more, onTzPage, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* ml = lv_label_create(more);
    lv_label_set_text(ml, "MORE");
    lv_obj_set_style_text_font(ml, F_MICRO, 0);
    lv_obj_center(ml);
    s_tzPageLbl = label(s_tzView, 152, 52 + TZ_ROWS * 42 + 18, "", C_INK3, F_NUM);
  }

  // ── SYSTEM ───────────────────────────────────────────────────────────────
  {
    lv_obj_t* p = s_pane[4];
    static const char* kRow[5] = { "Version", "Address", "Uptime", "Heap", "Last poll" };
    for (uint8_t i = 0; i < 5; i++) {
      lv_obj_t* r = row(p, i, kRow[i], nullptr);
      s_sysVal[i] = label(r, 220, (ROW_H - 13) / 2, "", C_INK2, F_NUM);
    }
    lv_obj_t* rb = lv_btn_create(p);
    lv_obj_set_size(rb, 150, 44);
    lv_obj_set_pos(rb, 16, 16 + 5 * ROW_PITCH + 8);
    lv_obj_set_style_radius(rb, R_MD, 0);
    lv_obj_set_style_border_width(rb, 0, 0);
    lv_obj_set_style_bg_color(rb, C_EDGE, 0);
    lv_obj_add_event_cb(rb, onReboot, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* l = lv_label_create(rb);
    lv_label_set_text(l, "REBOOT");
    lv_obj_set_style_text_font(l, F_MICRO, 0);
    lv_obj_center(l);
  }
}

// ── render ─────────────────────────────────────────────────────────────────
void uiSettingsRender() {
  if (!s_root) return;
  for (uint8_t i = 0; i < PANE_COUNT; i++) {
    const bool on = (i == s_tab);
    on ? lv_obj_clear_flag(s_pane[i], LV_OBJ_FLAG_HIDDEN)
       : lv_obj_add_flag(s_pane[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(s_seg[i], on ? C_EDGE_HI : C_FROST_2, 0);
    lv_obj_set_style_text_color(s_segLbl[i], on ? C_INK : C_INK3, 0);
  }

  for (uint8_t i = 0; i < DEN_COUNT; i++) {
    const bool on = (g_set.density == i);
    lv_obj_set_style_bg_color(s_denSeg[i], on ? C_EDGE_HI : C_FROST_2, 0);
    lv_obj_t* l = lv_obj_get_child(s_denSeg[i], 0);
    if (l) lv_obj_set_style_text_color(l, on ? C_INK : C_INK3, 0);
  }
  for (uint8_t i = 0; i < 2; i++) {
    const bool on = (g_set.clock24 == (i == 1));
    lv_obj_set_style_bg_color(s_clkSeg[i], on ? C_EDGE_HI : C_FROST_2, 0);
    lv_obj_t* l = lv_obj_get_child(s_clkSeg[i], 0);
    if (l) lv_obj_set_style_text_color(l, on ? C_INK : C_INK3, 0);
  }
  switchSet(s_swAlerts, g_set.alertsOn);
  switchSet(s_swFocus,  g_set.focusOn);
  switchSet(s_swQuiet,  g_set.quietOn);

  // favourites
  const uint8_t n = favCount();
  char cbuf[24];
  snprintf(cbuf, sizeof cbuf, "%u of %u", n, (unsigned)FAVS_MAX);
  lv_label_set_text(s_favCount, cbuf);
  const uint8_t pages = n ? (n + FAV_ROWS - 1) / FAV_ROWS : 1;
  if (s_favPage >= pages) s_favPage = 0;
  for (uint8_t i = 0; i < FAV_ROWS; i++) {
    const uint8_t idx = s_favPage * FAV_ROWS + i;
    char league[10], id[12];
    if (idx >= n || !favAt(idx, league, sizeof league, id, sizeof id)) {
      lv_obj_add_flag(s_favRow[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_clear_flag(s_favRow[i], LV_OBJ_FLAG_HIDDEN);
    // Resolve to a real name and colour when the team is on today's board;
    // otherwise show what we stored, which is honest rather than blank.
    const Game* g = nullptr;
    bool home = false;
    for (uint8_t k = 0; k < g_gameCount && !g; k++) {
      const Game& c = g_board[k];
      if (strcmp(c.league, league) != 0) continue;
      if (strcmp(c.away.id, id) == 0) { g = &c; home = false; }
      else if (strcmp(c.home.id, id) == 0) { g = &c; home = true; }
    }
    char line[48];
    if (g) {
      const Side& sd = home ? g->home : g->away;
      snprintf(line, sizeof line, "%s   %s", sd.abbr, sd.name);
      lv_obj_set_style_bg_color(s_favSwatch[i], lv_color_hex(teamInk(sd.color)), 0);
    } else {
      snprintf(line, sizeof line, "%s:%s", league, id);
      lv_obj_set_style_bg_color(s_favSwatch[i], C_EDGE_HI, 0);
    }
    lv_label_set_text(s_favLbl[i], line);
  }

  // network
  lv_label_set_text(s_netVal[0], WiFi.status() == WL_CONNECTED
                    ? WiFi.SSID().c_str() : "not connected");
  lv_label_set_text(s_netVal[1], g_set.proxy.length() ? g_set.proxy.c_str() : "not set");
  lv_label_set_text(s_netVal[2], g_set.token.length() ? "set" : "not set");
  lv_label_set_text(s_netVal[3], g_set.panelPass.length() ? "set" : "not set");
  {
    const int8_t ti = tzIndexOf(tzForProxy());
    lv_label_set_text(s_netVal[4], ti >= 0 ? kTimeZones[ti].label : "not set");
  }

  // system
  char b[40];
  lv_label_set_text(s_sysVal[0], SD_VERSION);
  lv_label_set_text(s_sysVal[1], WiFi.status() == WL_CONNECTED
                    ? WiFi.localIP().toString().c_str() : "-");
  const uint32_t up = millis() / 1000;
  snprintf(b, sizeof b, "%lud %02lu:%02lu", (unsigned long)(up / 86400),
           (unsigned long)((up % 86400) / 3600), (unsigned long)((up % 3600) / 60));
  lv_label_set_text(s_sysVal[2], b);
  snprintf(b, sizeof b, "%u K", (unsigned)(ESP.getFreeHeap() / 1024));
  lv_label_set_text(s_sysVal[3], b);
  const char* t = lastGoodClock();
  lv_label_set_text(s_sysVal[4], t[0] ? t : "-");
}

void uiSettingsOpen() {
  s_tab = 0;
  s_favPage = 0;
  if (s_testLbl) lv_label_set_text(s_testLbl, "");
  uiShow(SCR_SETTINGS);
  uiSettingsRender();
}

void uiSettingsTzOpen() { onTzOpen(nullptr); }

void uiSettingsTab(uint8_t i) { s_tab = i < PANE_COUNT ? i : 0; if (s_tab == 1) spKick(); uiSettingsRender(); }

void uiSettingsClose() {
  flushNow();
  // Density is applied on the way out, not on tap: uiInit() rebuilds the whole
  // screen tree and would pull this screen out from under itself.
  uiInit();
  uiBoardRefresh();
  uiShow(uiShouldIdle() ? SCR_IDLE : SCR_BOARD);
}

void uiSettingsTick() {
  if (g_catalogReady) {
    g_catalogReady = false;
    if (s_tab == 1 && uiCurrent() == SCR_SETTINGS) renderSports();
  }
  // The retry heartbeat: while the SPORTS pane is up without a catalog,
  // keep trying on the same 3 s cadence spKick() uses.
  if (s_tab == 1 && uiCurrent() == SCR_SETTINGS &&
      !g_catalogLoaded && !g_catalogInFlight &&
      s_spTries > 0 && s_spTries < 10 && millis() - s_spTryAt > 3000)
    spKick();
  if (s_dirty && millis() - s_dirtyAt > 1500) flushNow();
}
