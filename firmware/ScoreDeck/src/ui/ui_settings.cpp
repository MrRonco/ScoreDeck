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
#include <WiFi.h>

#define PANE_COUNT 3
#define ROW_H      56
#define ROW_PITCH  57
#define ROWS_MAX    6
#define FAV_ROWS    5

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
static lv_obj_t* s_favRow[FAV_ROWS];
static lv_obj_t* s_favSwatch[FAV_ROWS];
static lv_obj_t* s_favLbl[FAV_ROWS];
static lv_obj_t* s_favCount;
static uint8_t   s_favPage;

// NETWORK / SYSTEM panes
static lv_obj_t* s_netVal[4];
static lv_obj_t* s_sysVal[5];
static lv_obj_t* s_testLbl;

// Debounced save — see the file header.
static bool     s_dirty;
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
  lv_obj_set_style_bg_color(r, C_EDGE, LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_radius(r, 8, 0);
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
static void onTab(lv_event_t* e) {
  s_tab = (uint8_t)(intptr_t)lv_event_get_user_data(e);
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
  lv_obj_set_style_bg_color(s_root, C_PLATE, 0);
  lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
  lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* bar = glassPanel(s_root, 0, 0, SCR_W, 48, 0);
  label(bar, 20, 15, "SETTINGS", C_INK, F_ABBR);

  // Three panes do not justify a 176 px rail, so the selector is a segment.
  static const char* kTab[PANE_COUNT] = { "BOARD", "NETWORK", "SYSTEM" };
  for (uint8_t i = 0; i < PANE_COUNT; i++) {
    s_seg[i] = lv_btn_create(bar);
    lv_obj_set_size(s_seg[i], 120, 36);
    lv_obj_set_pos(s_seg[i], 216 + i * 124, 6);
    lv_obj_set_style_radius(s_seg[i], 8, 0);
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
  lv_obj_set_style_radius(done, 8, 0);
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
      lv_obj_set_style_radius(s_denSeg[i], 8, 0);
      lv_obj_set_style_border_width(s_denSeg[i], 0, 0);
      lv_obj_add_event_cb(s_denSeg[i], onDensity, LV_EVENT_CLICKED, (void*)(intptr_t)i);
      lv_obj_t* l = lv_label_create(s_denSeg[i]);
      lv_label_set_text(l, kDen[i]);
      lv_obj_set_style_text_font(l, F_MICRO, 0);
      lv_obj_center(l);
    }

    s_swAlerts = switchAt(row(p, 1, "Score alerts", onAlerts), g_set.alertsOn);
    s_swFocus  = switchAt(row(p, 2, "Open tense games automatically", onFocus), g_set.focusOn);
    s_swQuiet  = switchAt(row(p, 3, "Quiet hours", onQuiet), g_set.quietOn);

    lv_obj_t* fr = row(p, 4, "Your teams", nullptr);
    s_favCount = label(fr, 160, (ROW_H - 13) / 2, "", C_INK3, F_NUM);
    lv_obj_t* pageBtn = lv_btn_create(fr);
    lv_obj_set_size(pageBtn, 60, 38);
    lv_obj_set_pos(pageBtn, 736 - 8 - 60, (ROW_H - 38) / 2);
    lv_obj_set_style_radius(pageBtn, 8, 0);
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
      lv_obj_set_size(s_favRow[i], 736, 30);
      lv_obj_set_pos(s_favRow[i], 16, 16 + 5 * ROW_PITCH + i * 30);
      lv_obj_set_style_bg_opa(s_favRow[i], LV_OPA_TRANSP, 0);
      lv_obj_clear_flag(s_favRow[i], LV_OBJ_FLAG_SCROLLABLE);

      s_favSwatch[i] = lv_obj_create(s_favRow[i]);
      lv_obj_remove_style_all(s_favSwatch[i]);
      lv_obj_set_size(s_favSwatch[i], 5, 20);
      lv_obj_set_pos(s_favSwatch[i], 8, 5);
      lv_obj_set_style_radius(s_favSwatch[i], 2, 0);
      lv_obj_set_style_bg_opa(s_favSwatch[i], LV_OPA_COVER, 0);

      s_favLbl[i] = label(s_favRow[i], 24, 6, "", C_INK, F_BODY);

      lv_obj_t* up = lv_btn_create(s_favRow[i]);
      lv_obj_set_size(up, 44, 28);
      lv_obj_set_pos(up, 736 - 8 - 96, 1);
      lv_obj_set_style_radius(up, 6, 0);
      lv_obj_set_style_border_width(up, 0, 0);
      lv_obj_set_style_bg_color(up, C_EDGE, 0);
      lv_obj_add_event_cb(up, onFavUp, LV_EVENT_CLICKED, (void*)(intptr_t)i);
      lv_obj_t* ul = lv_label_create(up);
      lv_label_set_text(ul, "UP");
      lv_obj_set_style_text_font(ul, F_MICRO, 0);
      lv_obj_center(ul);

      lv_obj_t* rm = lv_btn_create(s_favRow[i]);
      lv_obj_set_size(rm, 44, 28);
      lv_obj_set_pos(rm, 736 - 8 - 44, 1);
      lv_obj_set_style_radius(rm, 6, 0);
      lv_obj_set_style_border_width(rm, 0, 0);
      lv_obj_set_style_bg_color(rm, C_EDGE, 0);
      lv_obj_add_event_cb(rm, onFavDrop, LV_EVENT_CLICKED, (void*)(intptr_t)i);
      lv_obj_t* rl = lv_label_create(rm);
      lv_label_set_text(rl, "X");
      lv_obj_set_style_text_font(rl, F_MICRO, 0);
      lv_obj_center(rl);
    }
    label(p, 16, 380, "Add teams in the browser  -  everything else lives there too",
          C_INK3, F_MICRO);
  }

  // ── NETWORK ──────────────────────────────────────────────────────────────
  {
    lv_obj_t* p = s_pane[1];
    static const char* kRow[4] = { "Wi-Fi", "Proxy", "Proxy token", "Panel password" };
    for (uint8_t i = 0; i < 4; i++) {
      lv_obj_t* r = row(p, i, kRow[i], nullptr);
      s_netVal[i] = label(r, 220, (ROW_H - 15) / 2, "", C_INK2, F_BODY);
      lv_obj_set_width(s_netVal[i], 380);
      lv_label_set_long_mode(s_netVal[i], LV_LABEL_LONG_DOT);

      if (i == 1) {
        lv_obj_t* t = lv_btn_create(r);
        lv_obj_set_size(t, 76, 38);
        lv_obj_set_pos(t, 736 - 8 - 76, (ROW_H - 38) / 2);
        lv_obj_set_style_radius(t, 8, 0);
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
      if (i >= 2) {
        // The panel is the password-recovery path on purpose: physical access
        // is already total access, and a forgotten password must not brick
        // the portal.
        lv_obj_t* cbtn = lv_btn_create(r);
        lv_obj_set_size(cbtn, 88, 38);
        lv_obj_set_pos(cbtn, 736 - 8 - 88, (ROW_H - 38) / 2);
        lv_obj_set_style_radius(cbtn, 8, 0);
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
  }

  // ── SYSTEM ───────────────────────────────────────────────────────────────
  {
    lv_obj_t* p = s_pane[2];
    static const char* kRow[5] = { "Version", "Address", "Uptime", "Heap", "Last poll" };
    for (uint8_t i = 0; i < 5; i++) {
      lv_obj_t* r = row(p, i, kRow[i], nullptr);
      s_sysVal[i] = label(r, 220, (ROW_H - 13) / 2, "", C_INK2, F_NUM);
    }
    lv_obj_t* rb = lv_btn_create(p);
    lv_obj_set_size(rb, 150, 44);
    lv_obj_set_pos(rb, 16, 16 + 5 * ROW_PITCH + 8);
    lv_obj_set_style_radius(rb, 8, 0);
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

void uiSettingsTab(uint8_t i) { s_tab = i < PANE_COUNT ? i : 0; uiSettingsRender(); }

void uiSettingsClose() {
  flushNow();
  // Density is applied on the way out, not on tap: uiInit() rebuilds the whole
  // screen tree and would pull this screen out from under itself.
  uiInit();
  uiBoardRefresh();
  uiShow(uiShouldIdle() ? SCR_IDLE : SCR_BOARD);
}

void uiSettingsTick() {
  if (s_dirty && millis() - s_dirtyAt > 1500) flushNow();
}
