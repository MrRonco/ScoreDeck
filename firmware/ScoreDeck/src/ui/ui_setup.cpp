// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
// ui_setup.cpp — first-boot onboarding on the panel.
//
// Wi-Fi has to be entered here: there is no network to serve a portal over yet.
// Once connected the user chooses — finish on the panel, or open the browser,
// where searching 3,000 teams is not miserable. UI.md §6 / PLAN.md §6.
#include "ui.h"
#include "theme.h"
#include "../config.h"
#include "../core/state.h"
#include <WiFi.h>

static lv_obj_t* s_root;
static lv_obj_t* s_kb;
static lv_obj_t* s_taSsid;
static lv_obj_t* s_taPass;
static lv_obj_t* s_taProxy;
static lv_obj_t* s_lblHint;
static lv_obj_t* s_progWrap, *s_prog;
static lv_obj_t* s_lblIp;
static bool      s_active = false;

bool uiSetupActive() { return s_active; }
lv_obj_t* uiSetupRoot() { return s_root; }

static void hint(const char* t, lv_color_t c) {
  lv_label_set_text(s_lblHint, t);
  lv_obj_set_style_text_color(s_lblHint, c, 0);
}

static void onFocus(lv_event_t* e) {
  lv_obj_t* ta = lv_event_get_target(e);
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_FOCUSED) {
    lv_keyboard_set_textarea(s_kb, ta);
    // A password field wants the same layout, not a number pad.
    lv_keyboard_set_mode(s_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
  } else if (code == LV_EVENT_DEFOCUSED) {
    lv_keyboard_set_textarea(s_kb, nullptr);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
  }
}

static void onConnect(lv_event_t*) {
  const char* ssid = lv_textarea_get_text(s_taSsid);
  const char* pass = lv_textarea_get_text(s_taPass);
  if (!ssid || !*ssid) { hint("Enter a network name first.", C_INK); return; }

  hint("Connecting to that network...", C_INK2);
  lv_obj_clear_flag(s_progWrap, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_width(s_prog, 0);
  lv_refr_now(nullptr);                     // paint before we block

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  const uint32_t t0 = millis();
  int lastW = -1;
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 12000) {
    delay(200);
    // A determinate bar, because the wait is bounded and we know the bound.
    // Change-gated: a width write per 200 ms tick would repaint the strip for
    // nothing most of the time.
    const int w = (int)((millis() - t0) * 300 / 12000);
    if (w != lastW) { lastW = w; lv_obj_set_width(s_prog, w); }
    lv_timer_handler();
  }
  lv_obj_add_flag(s_progWrap, LV_OBJ_FLAG_HIDDEN);

  if (WiFi.status() != WL_CONNECTED) {
    // S_ALERT, not body ink. Failure and success were typographically
    // identical here — same slot, same face, differing only in lightness.
    hint("Could not join that network. Check the name and password.", S_ALERT);
    return;
  }
  // Modem-sleep wake bursts contend with the panel DMA and wiggle the screen.
  WiFi.setSleep(false);

  g_set.ssid = ssid;
  if (*pass) g_set.pass = pass;             // never overwrite a stored secret with blank
  const char* px = lv_textarea_get_text(s_taProxy);
  if (px && *px) g_set.proxy = px;
  settingsSave();

  char msg[96];
  snprintf(msg, sizeof msg, "Connected.  Open  http://%s/  in a browser.",
           WiFi.localIP().toString().c_str());
  lv_label_set_text(s_lblIp, msg);
  lv_obj_clear_flag(s_lblIp, LV_OBJ_FLAG_HIDDEN);
  hint(g_set.proxy.length() ? "Ready." : "Add your proxy URL below, or use the browser.",
       C_INK2);
}

static void onDone(lv_event_t*) {
  const char* px = lv_textarea_get_text(s_taProxy);
  if (px && *px) g_set.proxy = px;
  settingsSave();
  s_active = false;
  uiShow(SCR_BOARD);
}

/** Open the keyboard as a field focus would, so it can be reviewed headless. */
void uiSetupShowKeyboard() {
  if (!s_kb || !s_taSsid) return;
  lv_keyboard_set_textarea(s_kb, s_taSsid);
  lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(s_kb);
}

void uiSetupInit(lv_obj_t* parent) {
  s_active = true;
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

  // 252 tall, ending at y=272 — every CONTROL stays above the keyboard's 280,
  // so Connect is still reachable while a field is focused. The reference
  // material that does not need to be reachable goes below, into the 220 rows
  // this screen used to leave empty (45.8% of the first screen anyone sees).
  lv_obj_t* card = glassPanel(s_root, 40, 20, SCR_W - 80, 252, R_LG);

  lv_obj_t* h = lv_label_create(card);
  // The wordmark is set in caps because the display face has no lowercase, and
  // this is the first thing anyone ever sees on the panel — it rendered as nine
  // hollow boxes.
  lv_label_set_text(h, "SCOREDECK");
  lv_obj_set_style_text_font(h, F_DISPLAY, 0);
  lv_obj_set_style_text_color(h, C_INK, 0);
  lv_obj_set_pos(h, 22, 12);

  lv_obj_t* sub = lv_label_create(card);
  lv_label_set_text(sub, "Join a network to get started");
  lv_obj_set_style_text_font(sub, F_BODY, 0);
  lv_obj_set_style_text_color(sub, C_INK3, 0);
  lv_obj_set_pos(sub, 24, 48);

  // A LABEL above each field, not a placeholder inside it. A placeholder is
  // gone the moment you type, so the one moment you might want to check what
  // a box is for is the moment it stops saying.
  auto fieldLabel = [&](const char* txt, int y) {
    lv_obj_t* l = lv_label_create(card);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, F_MICRO, 0);
    lv_obj_set_style_text_color(l, C_INK3, 0);
    lv_obj_set_style_text_letter_space(l, 1, 0);
    lv_obj_set_pos(l, 24, y);
    return l;
  };

  auto field = [&](const char* ph, int y, bool pw) {
    lv_obj_t* ta = lv_textarea_create(card);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, ph);
    lv_textarea_set_password_mode(ta, pw);
    // One width for all three. They shipped at 320/320/460, so nothing on the
    // screen shared a right edge with anything else.
    lv_obj_set_size(ta, 330, 38);
    lv_obj_set_pos(ta, 22, y);
    lv_obj_set_style_bg_color(ta, C_FROST_2, 0);
    lv_obj_set_style_border_color(ta, C_EDGE, 0);
    lv_obj_set_style_radius(ta, R_MD, 0);
    lv_obj_set_style_text_color(ta, C_INK, 0);
    lv_obj_set_style_text_font(ta, F_BODY, 0);
    lv_obj_add_event_cb(ta, onFocus, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(ta, onFocus, LV_EVENT_DEFOCUSED, nullptr);
    return ta;
  };

  fieldLabel("WI-FI NETWORK", 74);
  s_taSsid  = field("Network name", 88, false);
  fieldLabel("PASSWORD", 132);
  s_taPass  = field("Password", 146, true);
  fieldLabel("PROXY URL", 190);
  s_taProxy = field("http://192.168.1.50:8787", 204, false);

  if (g_set.ssid.length())  lv_textarea_set_text(s_taSsid, g_set.ssid.c_str());
  if (g_set.proxy.length()) lv_textarea_set_text(s_taProxy, g_set.proxy.c_str());

  // ── the right column: what this is, and the two actions ──────────────────
  auto rlbl = [&](const char* txt, int y, const lv_font_t* f, lv_color_t c, int w) {
    lv_obj_t* l = lv_label_create(card);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_obj_set_width(l, w);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(l, 386, y);
    return l;
  };
  rlbl("WHAT THE PROXY IS", 78, F_MICRO, C_INK3, 300);
  rlbl("A small service on your own network that fetches scores and hands them "
       "to the panel. You can set it later from a browser.",
       96, F_BODY, C_INK2, 300);

  auto button = [&](const char* txt, int x, int y, lv_event_cb_t cb) {
    lv_obj_t* b = lv_btn_create(card);
    lv_obj_set_size(b, 150, 44);
    lv_obj_set_pos(b, x, y);
    uiButton(b);
    lv_obj_set_style_bg_color(b, C_EDGE, 0);
    lv_obj_set_style_border_color(b, C_EDGE_HI, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_radius(b, R_MD, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, F_BODY, 0);
    lv_obj_set_style_text_color(l, C_INK, 0);
    lv_obj_center(l);
    return b;
  };
  // Connect is the product being set up; Continue abandons setup and drops the
  // user on an empty board. They used to be the same button.
  lv_obj_t* bConnect = button("Connect", 386, 168, onConnect);
  uiPrimaryButton(bConnect);
  button("Skip for now", 550, 168, onDone);

  s_lblHint = lv_label_create(card);
  lv_obj_set_pos(s_lblHint, 386, 224);
  lv_obj_set_width(s_lblHint, 300);
  lv_label_set_long_mode(s_lblHint, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(s_lblHint, F_MICRO, 0);
  lv_obj_set_style_text_color(s_lblHint, C_INK3, 0);
  lv_label_set_text(s_lblHint, "");

  // The 12-second connect attempt gets a bar. It used to block with one 13 px
  // word — about a third of one percent of the panel lit — for twelve seconds.
  // C_EDGE_HI, never A_LIVE: there is no live game on this screen and the
  // accent has one meaning.
  s_progWrap = lv_obj_create(card);
  lv_obj_remove_style_all(s_progWrap);
  lv_obj_set_size(s_progWrap, 300, 4);
  lv_obj_set_pos(s_progWrap, 386, 214);
  lv_obj_set_style_bg_color(s_progWrap, C_EDGE, 0);
  lv_obj_set_style_bg_opa(s_progWrap, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(s_progWrap, R_XS, 0);
  lv_obj_add_flag(s_progWrap, LV_OBJ_FLAG_HIDDEN);
  s_prog = lv_obj_create(s_progWrap);
  lv_obj_remove_style_all(s_prog);
  lv_obj_set_size(s_prog, 0, 4);
  lv_obj_set_pos(s_prog, 0, 0);
  lv_obj_set_style_bg_color(s_prog, C_EDGE_HI, 0);
  lv_obj_set_style_bg_opa(s_prog, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(s_prog, R_XS, 0);

  // ── below the card: the 220 rows that used to be empty plate ─────────────
  //
  // Reference, not controls — the keyboard covers this when it opens and
  // nothing here needs to be reachable while typing.
  auto note = [&](const char* txt, int x, int y, const lv_font_t* f, lv_color_t c, int w) {
    lv_obj_t* l = lv_label_create(s_root);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_obj_set_width(l, w);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(l, x, y);
    return l;
  };
  note("WHAT HAPPENS NEXT", 62, 296, F_MICRO, C_INK3, 300);
  note("1.  The panel joins your network.", 62, 318, F_BODY, C_INK2, 330);
  note("2.  It shows its own address here.", 62, 342, F_BODY, C_INK2, 330);
  note("3.  Finish the rest in a browser — teams, alerts, quiet hours.",
       62, 366, F_BODY, C_INK2, 400);

  s_lblIp = note("", 62, 400, F_BODY, C_INK, 660);
  lv_obj_add_flag(s_lblIp, LV_OBJ_FLAG_HIDDEN);

  // The keyboard is 40% of the panel. It stays hidden until a field is focused.
  s_kb = lv_keyboard_create(s_root);
  lv_obj_set_size(s_kb, SCR_W, 200);
  // ALIGN, not set_pos. lv_keyboard_create() bottom-aligns itself, and
  // lv_obj_set_pos() on an aligned object sets the align OFFSET — so
  // set_pos(0, SCR_H - 200) placed the keyboard 280 px BELOW the bottom of a
  // 480 px screen. Measured y = 560: the on-device keyboard has never been on
  // screen at all, which means first-run text entry had no keyboard. It was
  // invisible to review because nothing could open it headless until now.
  lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  keyboardTheme(s_kb);
  lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
}
