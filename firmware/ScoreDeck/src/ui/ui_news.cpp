// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
// ui_news.cpp — UI.md §5.4.
//
// Headlines only, with the summary the proxy already trimmed. There is no
// article body and no HTML parser on the device, deliberately: the panel is
// something you glance at, and a 240-character summary is the honest limit of
// what it can usefully show.
#include "ui.h"
#include "theme.h"
#include "../config.h"
#include "../core/state.h"
#include "../net/api.h"
#include <time.h>

#define NEWS_ROWS 5

static lv_obj_t* s_root;
static lv_obj_t* s_hint;
static lv_obj_t* s_card[NEWS_ROWS];
static lv_obj_t* s_chip[NEWS_ROWS];
static lv_obj_t* s_chipLbl[NEWS_ROWS];
static lv_obj_t* s_head[NEWS_ROWS];
static lv_obj_t* s_desc[NEWS_ROWS];
static lv_obj_t* s_when[NEWS_ROWS];
static int8_t    s_expanded = -1;
static uint8_t   s_scroll;

lv_obj_t* uiNewsRoot() { return s_root; }

static lv_obj_t* lb(lv_obj_t* p, int x, int y, lv_color_t c, const lv_font_t* f,
                    lv_text_align_t a = LV_TEXT_ALIGN_LEFT, int w = 0) {
  lv_obj_t* l = lv_label_create(p);
  lv_obj_set_pos(l, x, y);
  lv_obj_set_style_text_color(l, c, 0);
  lv_obj_set_style_text_font(l, f, 0);
  if (w) { lv_obj_set_width(l, w); lv_obj_set_style_text_align(l, a, 0); }
  lv_label_set_text(l, "");
  return l;
}

static void relTime(uint32_t when, char* out, size_t cap) {
  const time_t now = time(nullptr);
  if (!when || now < 100000) { snprintf(out, cap, ""); return; }
  long d = (long)now - (long)when;
  if (d < 90)          snprintf(out, cap, "now");
  else if (d < 5400)   snprintf(out, cap, "%ldm ago", d / 60);
  else if (d < 86400)  snprintf(out, cap, "%ldh ago", d / 3600);
  else                 snprintf(out, cap, "%ldd ago", d / 86400);
}

static void onBack(lv_event_t*) { uiNewsClose(); }

static void onItem(lv_event_t* e) {
  const int row = (int)(intptr_t)lv_event_get_user_data(e);
  const int idx = s_scroll + row;
  if (idx >= g_news.count) return;
  // Every headline opens the reader; with no readable body it shows the
  // summary there instead — one gesture, one destination.
  uiReaderOpen(g_news.items[idx]);
}

static void onGesture(lv_event_t*) {
  const lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_get_act());
  if (d == LV_DIR_TOP && s_scroll + NEWS_ROWS < g_news.count) { s_scroll++; s_expanded = -1; uiNewsRender(); }
  else if (d == LV_DIR_BOTTOM && s_scroll) { s_scroll--; s_expanded = -1; uiNewsRender(); }
}

void uiNewsInit(lv_obj_t* parent) {
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
  lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(s_root, onGesture, LV_EVENT_GESTURE, nullptr);

  lv_obj_t* bar = glassPanel(s_root, 0, 0, SCR_W, BAR_H, 0);
  lv_obj_t* back = lv_btn_create(bar);
  lv_obj_set_size(back, 48, 34);
  lv_obj_set_pos(back, 14, 7);
  lv_obj_set_style_bg_color(back, C_EDGE, 0);
  lv_obj_set_style_border_width(back, 0, 0);
  lv_obj_set_style_radius(back, 8, 0);
  lv_obj_add_event_cb(back, onBack, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* bl = lv_label_create(back);
  lv_label_set_text(bl, "<");
  lv_obj_set_style_text_font(bl, F_BODY, 0);   // F_ABBR has no glyph for "<"
  lv_obj_set_style_text_color(bl, C_INK, 0);
  lv_obj_center(bl);
  lv_obj_t* ttl = lb(bar, 74, 15, C_INK, F_ABBR);
  lv_label_set_text(ttl, "NEWS");
  s_hint = lb(bar, SCR_W - 18 - 300, 17, C_INK3, F_MICRO, LV_TEXT_ALIGN_RIGHT, 300);

  for (uint8_t i = 0; i < NEWS_ROWS; i++) {
    const int y = 58 + i * 80;
    s_card[i] = glassPanel(s_root, 16, y, 768, 72, 12);
    lv_obj_add_flag(s_card[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_card[i], LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_card[i], onItem, LV_EVENT_SHORT_CLICKED, (void*)(intptr_t)i);
    lv_obj_add_event_cb(s_card[i], onGesture, LV_EVENT_GESTURE, nullptr);

    s_chip[i] = teamBadge(s_card[i], "", 0x5D6D7E, 26);
    lv_obj_set_pos(s_chip[i], 14, 14);
    s_chipLbl[i] = lv_obj_get_child(s_chip[i], 0);

    s_head[i] = lb(s_card[i], 52, 12, C_INK, F_BODY);
    lv_obj_set_width(s_head[i], 600);
    lv_label_set_long_mode(s_head[i], LV_LABEL_LONG_DOT);

    s_desc[i] = lb(s_card[i], 52, 36, C_INK3, F_BODY);   // upstream prose
    lv_obj_set_width(s_desc[i], 690);
    lv_label_set_long_mode(s_desc[i], LV_LABEL_LONG_WRAP);

    s_when[i] = lb(s_card[i], 664, 13, C_INK3, F_NUM, LV_TEXT_ALIGN_RIGHT, 90);
  }
}

void uiNewsRender() {
  const NewsFeed& n = g_news;
  char buf[48];
  if (!n.count) {
    lv_label_set_text(s_hint, n.loading ? "loading" : "nothing yet");
  } else {
    snprintf(buf, sizeof buf, "%u stories", n.count);
    lv_label_set_text(s_hint, buf);
  }

  int y = 58;
  for (uint8_t r = 0; r < NEWS_ROWS; r++) {
    const uint8_t idx = s_scroll + r;
    if (idx >= n.count) {
      lv_obj_add_flag(s_card[r], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    const NewsItem& it = n.items[idx];
    lv_obj_clear_flag(s_card[r], LV_OBJ_FLAG_HIDDEN);

    const bool open = (s_expanded == idx);
    // A card only grows for the story you asked to read; the rest stay compact
    // so the list keeps its shape.
    const int h = open ? 128 : 72;
    lv_obj_set_pos(s_card[r], 16, y);
    lv_obj_set_size(s_card[r], 768, h);
    y += h + 8;

    if (it.abbr[0]) {
      lv_obj_clear_flag(s_chip[r], LV_OBJ_FLAG_HIDDEN);
      teamBadgeSet(s_chip[r], it.color);
      lv_label_set_text(s_chipLbl[r], it.abbr);
      lv_obj_set_x(s_head[r], 52);
      lv_obj_set_x(s_desc[r], 52);
    } else {
      lv_obj_add_flag(s_chip[r], LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_x(s_head[r], 16);
      lv_obj_set_x(s_desc[r], 16);
    }

    lv_label_set_text(s_head[r], it.headline);
    lv_obj_set_style_text_color(s_head[r], it.abbr[0] ? C_INK : C_INK2, 0);

    if (open && it.desc[0]) {
      lv_obj_clear_flag(s_desc[r], LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(s_desc[r], it.desc);
    } else {
      lv_obj_add_flag(s_desc[r], LV_OBJ_FLAG_HIDDEN);
    }

    char w[16];
    relTime(it.when, w, sizeof w);
    lv_label_set_text(s_when[r], w);
  }
}

void uiNewsOpen() {
  s_scroll = 0;
  s_expanded = -1;
  g_news.loading = true;
  uiShow(SCR_NEWS);
  uiNewsRender();
  apiNewsStart();
}

void uiNewsClose() {
  uiReaderHide();
  uiShow(uiShouldIdle() ? SCR_IDLE : SCR_BOARD);
}
bool uiNewsIsOpen() { return uiCurrent() == SCR_NEWS; }
