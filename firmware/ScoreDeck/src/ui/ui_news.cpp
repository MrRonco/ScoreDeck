// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
// ui_news.cpp — UI.md §5.4.
//
// Headlines only, with the summary the proxy already trimmed. There is no
// article body and no HTML parser on the device, deliberately: the panel is
// something you glance at, and a 240-character summary is the honest limit of
// what it can usefully show.
//
// It used to be headlines-plus-an-expanding-summary. The expansion index was
// only ever assigned -1 — never a row index — so `open` was false on every
// pass, and the 128 px branch and the five wrap-capable summary labels were
// unreachable code. The summary was only ever readable in the reader anyway
// (onItem sends every headline there). The dead state is gone; what is left
// is the list the screen actually drew.
#include "ui.h"
#include "theme.h"
#include "../config.h"
#include "../core/state.h"
#include "../net/api.h"
#include <time.h>

#define NEWS_ROWS 5
#define NEWS_ROW_H 72
#define NEWS_ROW_Y 58
// The slate api.cpp already hands us when a story names no team (api.cpp:709,
// `it["c"] | 0x5D6D7E`). Named here so the chip and the feed agree.
#define NEWS_CHIP_NEUTRAL 0x5D6D7E

static lv_obj_t* s_root;
static lv_obj_t* s_hint;
static lv_obj_t* s_more;           // "there is more below" — the sixth story
static lv_obj_t* s_card[NEWS_ROWS];
static lv_obj_t* s_chip[NEWS_ROWS];
static lv_obj_t* s_chipLbl[NEWS_ROWS];
static lv_obj_t* s_head[NEWS_ROWS];
static lv_obj_t* s_when[NEWS_ROWS];
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
  if (d == LV_DIR_TOP && s_scroll + NEWS_ROWS < g_news.count) { s_scroll++; uiNewsRender(); }
  else if (d == LV_DIR_BOTTOM && s_scroll) { s_scroll--; uiNewsRender(); }
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
  // Clickable only so the swipe reaches it — there is no tap handler here, so
  // it is an input surface and not a control. uiTapZone() is that statement.
  uiTapZone(s_root);
  lv_obj_add_event_cb(s_root, onGesture, LV_EVENT_GESTURE, nullptr);

  lv_obj_t* bar = glassPanel(s_root, 0, 0, SCR_W, BAR_H, 0);
  // Bare chevron: the title sits at x=74 and a worded chip measures 88 px.
  backChip(bar, nullptr, onBack);
  lv_obj_t* ttl = lb(bar, 74, 15, C_INK, F_ABBR);
  lv_label_set_text(ttl, "NEWS");
  s_hint = lb(bar, SCR_W - 18 - 300, 17, C_INK3, F_MICRO, LV_TEXT_ALIGN_RIGHT, 300);

  // The list scrolls, and until now nothing on the screen said so — the sixth
  // story existed only for a gesture nobody had been told about. Same words,
  // same face and the same y as the reader's one-time hint, because it is the
  // same promise.
  s_more = lb(s_root, (SCR_W - 300) / 2, 456, C_INK3, F_MICRO,
              LV_TEXT_ALIGN_CENTER, 300);
  lv_obj_set_style_text_letter_space(s_more, 1, 0);
  lv_obj_add_flag(s_more, LV_OBJ_FLAG_HIDDEN);

  for (uint8_t i = 0; i < NEWS_ROWS; i++) {
    const int y = NEWS_ROW_Y + i * (NEWS_ROW_H + 8);
    s_card[i] = glassPanel(s_root, 16, y, 768, NEWS_ROW_H, R_LG);
    uiPressable(s_card[i]);
    lv_obj_clear_flag(s_card[i], LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_card[i], onItem, LV_EVENT_SHORT_CLICKED, (void*)(intptr_t)i);
    lv_obj_add_event_cb(s_card[i], onGesture, LV_EVENT_GESTURE, nullptr);

    s_chip[i] = teamBadge(s_card[i], "", NEWS_CHIP_NEUTRAL, 26);
    lv_obj_set_pos(s_chip[i], 14, 14);
    s_chipLbl[i] = lv_obj_get_child(s_chip[i], 0);

    s_head[i] = lb(s_card[i], 52, 12, C_INK, F_BODY);
    lv_obj_set_width(s_head[i], 600);
    lv_label_set_long_mode(s_head[i], LV_LABEL_LONG_DOT);

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

  for (uint8_t r = 0; r < NEWS_ROWS; r++) {
    const uint8_t idx = s_scroll + r;
    if (idx >= n.count) {
      lv_obj_add_flag(s_card[r], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    const NewsItem& it = n.items[idx];
    lv_obj_clear_flag(s_card[r], LV_OBJ_FLAG_HIDDEN);

    // A story with no team badge is still a story. It used to lose its chip,
    // shift its headline 37 px left (first-ink 70 -> 33) and drop a full ink
    // tier (14.75:1 -> 8.26:1) — three signals that all read as "lesser",
    // for the sole crime of not being about a club. The chip stays, in the
    // feed's own neutral slate; x and ink do not move.
    teamBadgeSet(s_chip[r], it.abbr[0] ? it.color : NEWS_CHIP_NEUTRAL);
    lv_label_set_text(s_chipLbl[r], it.abbr);

    lv_label_set_text(s_head[r], it.headline);

    char w[16];
    relTime(it.when, w, sizeof w);
    lv_label_set_text(s_when[r], w);
  }

  // Only two states are true, so only two are offered.
  if (s_scroll + NEWS_ROWS < n.count) {
    lv_label_set_text(s_more, "SWIPE UP FOR MORE");
    lv_obj_clear_flag(s_more, LV_OBJ_FLAG_HIDDEN);
  } else if (s_scroll) {
    lv_label_set_text(s_more, "SWIPE DOWN FOR THE LATEST");
    lv_obj_clear_flag(s_more, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(s_more, LV_OBJ_FLAG_HIDDEN);
  }
}

void uiNewsOpen() {
  // Opening the list means the list is what you get. The reader's root is
  // transparent now, so an article left up over a re-opened list would let
  // five cards read straight through it.
  uiReaderHide();
  s_scroll = 0;
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
