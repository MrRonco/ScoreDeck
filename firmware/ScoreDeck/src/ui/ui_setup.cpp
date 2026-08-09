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

  hint("Connecting...", C_INK2);
  lv_refr_now(nullptr);                     // paint before we block

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  const uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 12000) {
    delay(200);
    lv_timer_handler();
  }

  if (WiFi.status() != WL_CONNECTED) {
    hint("Could not join that network. Check the name and password.", C_INK);
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
  snprintf(msg, sizeof msg, "Connected. Finish setup at  http://%s/",
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

void uiSetupInit(lv_obj_t* parent) {
  s_active = true;
  s_root = lv_obj_create(parent);
  lv_obj_remove_style_all(s_root);
  lv_obj_set_size(s_root, SCR_W, SCR_H);
  lv_obj_set_style_bg_color(s_root, C_PLATE, 0);
  lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
  lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* card = glassPanel(s_root, 40, 24, SCR_W - 80, 236, 14);

  lv_obj_t* h = lv_label_create(card);
  // The wordmark is set in caps because the display face has no lowercase, and
  // this is the first thing anyone ever sees on the panel — it rendered as nine
  // hollow boxes.
  lv_label_set_text(h, "SCOREDECK");
  lv_obj_set_style_text_font(h, F_DISPLAY, 0);
  lv_obj_set_style_text_color(h, C_INK, 0);
  lv_obj_set_pos(h, 22, 14);

  lv_obj_t* sub = lv_label_create(card);
  lv_label_set_text(sub, "Join a network to get started");
  lv_obj_set_style_text_font(sub, F_BODY, 0);
  lv_obj_set_style_text_color(sub, C_INK3, 0);
  lv_obj_set_pos(sub, 24, 52);

  auto field = [&](const char* ph, int y, bool pw) {
    lv_obj_t* ta = lv_textarea_create(card);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, ph);
    lv_textarea_set_password_mode(ta, pw);
    lv_obj_set_size(ta, 320, 40);
    lv_obj_set_pos(ta, 22, y);
    lv_obj_set_style_bg_color(ta, C_FROST_2, 0);
    lv_obj_set_style_border_color(ta, C_EDGE, 0);
    lv_obj_set_style_text_color(ta, C_INK, 0);
    lv_obj_set_style_text_font(ta, F_BODY, 0);
    lv_obj_add_event_cb(ta, onFocus, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(ta, onFocus, LV_EVENT_DEFOCUSED, nullptr);
    return ta;
  };
  s_taSsid  = field("Network name", 84, false);
  s_taPass  = field("Password", 132, true);
  s_taProxy = field("Proxy URL  (http://192.168.1.50:8787)", 180, false);
  lv_obj_set_width(s_taProxy, 460);

  if (g_set.ssid.length())  lv_textarea_set_text(s_taSsid, g_set.ssid.c_str());
  if (g_set.proxy.length()) lv_textarea_set_text(s_taProxy, g_set.proxy.c_str());

  auto button = [&](const char* txt, int x, int y, lv_event_cb_t cb) {
    lv_obj_t* b = lv_btn_create(card);
    lv_obj_set_size(b, 150, 44);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_bg_color(b, C_EDGE, 0);
    lv_obj_set_style_border_color(b, C_EDGE_HI, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_radius(b, 9, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, F_BODY, 0);
    lv_obj_set_style_text_color(l, C_INK, 0);
    lv_obj_center(l);
    return b;
  };
  button("Connect", 360, 84, onConnect);
  button("Continue", 530, 84, onDone);

  s_lblHint = lv_label_create(card);
  lv_obj_set_pos(s_lblHint, 24, 208);
  lv_obj_set_style_text_font(s_lblHint, F_MICRO, 0);
  lv_obj_set_style_text_color(s_lblHint, C_INK3, 0);
  lv_label_set_text(s_lblHint, "");

  s_lblIp = lv_label_create(s_root);
  lv_obj_set_pos(s_lblIp, 44, 268);
  lv_obj_set_style_text_font(s_lblIp, F_BODY, 0);
  lv_obj_set_style_text_color(s_lblIp, C_INK2, 0);
  lv_label_set_text(s_lblIp, "");
  lv_obj_add_flag(s_lblIp, LV_OBJ_FLAG_HIDDEN);

  // The keyboard is 40% of the panel. It stays hidden until a field is focused.
  s_kb = lv_keyboard_create(s_root);
  lv_obj_set_size(s_kb, SCR_W, 200);
  lv_obj_set_pos(s_kb, 0, SCR_H - 200);
  lv_obj_set_style_bg_color(s_kb, C_FROST_2, 0);
  lv_obj_set_style_text_font(s_kb, F_BODY, 0);
  lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
}
