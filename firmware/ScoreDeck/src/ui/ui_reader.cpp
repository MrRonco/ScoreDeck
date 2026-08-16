// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
// ui_reader.cpp — the article reader. UI.md §5.4.
//
// Drag-to-scroll, by request. The first cut was paged with tap zones — crisp
// one-shot repaints — but the gesture everyone actually makes on a screen of
// text is to drag it, and an affordance you have to be taught loses to the
// one your thumb already knows. LVGL's native vertical scroll with momentum
// does the work; the visible text band is ~560x330 px, so a moving frame
// costs ~100 ms of flush (~10 fps under the finger). That is e-reader feel,
// not phone feel — the measured, accepted trade for direct manipulation on
// this panel. If it reads as jank on glass, the fallback is drag-flips-page.
//
// The body sits in a centred 560 px column — ~70 characters of F_BODY per
// line, book measure, instead of 100+ character full-bleed lines nobody can
// track back across. The scroll CONTAINER spans the full panel width though,
// so a drag that starts in the margins still scrolls the story.
//
// The reader is an OVERLAY above the news list, not a Screen enum member:
// uiCurrent() stays SCR_NEWS while it is up, so the main loop's
// never-navigate-away guard (which already protects news) covers it with no
// new plumbing, and back simply unhides the list beneath.
#include "ui.h"
#include "theme.h"
#include "../config.h"
#include "../core/state.h"
#include "../net/api.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

#define RD_COL_X   120
#define RD_COL_W   560
#define RD_BODY_Y  116

static lv_obj_t* s_root;
static lv_obj_t* s_headline;
static lv_obj_t* s_meta;
static lv_obj_t* s_scroll;         // the scrollable band below the header
static lv_obj_t* s_body;           // the one label holding the whole story

static NewsItem s_item;            // the article being read (copied — the
                                   // feed can refresh underneath us)
static bool     s_waiting;         // story fetch in flight for s_item

// ── content states ─────────────────────────────────────────────────────────
static void setBody(const char* text) {
  lv_label_set_text(s_body, text);
  lv_obj_scroll_to_y(s_scroll, 0, LV_ANIM_OFF);
}

static void renderStory() {
  lv_label_set_text(s_headline,
                    g_story.headline[0] ? g_story.headline : s_item.headline);
  setBody(g_story.body);
}

/** No body (no id, premium, or the fetch failed): the summary is the story. */
static void renderSummary(const char* note) {
  lv_label_set_text(s_headline, s_item.headline);
  static char buf[300];
  snprintf(buf, sizeof buf, "%s%s%s", s_item.desc,
           note ? "\n\n" : "", note ? note : "");
  setBody(buf);
}

static void metaLine() {
  char when[16], buf[48];
  // Same vocabulary as the list beneath (ui_news relTime).
  const time_t now = time(nullptr);
  when[0] = '\0';
  if (s_item.when && now >= 100000) {
    const long d = (long)now - (long)s_item.when;
    if (d < 90)         snprintf(when, sizeof when, "NOW");
    else if (d < 5400)  snprintf(when, sizeof when, "%ldM AGO", d / 60);
    else if (d < 86400) snprintf(when, sizeof when, "%ldH AGO", d / 3600);
    else                snprintf(when, sizeof when, "%ldD AGO", d / 86400);
  }
  if (s_item.abbr[0]) snprintf(buf, sizeof buf, "%s  ·  %s", s_item.abbr, when);
  else                snprintf(buf, sizeof buf, "%s", when);
  lv_label_set_text(s_meta, buf);
}

// ── input ──────────────────────────────────────────────────────────────────
static void onBack(lv_event_t*) { uiReaderHide(); }

// ── lifecycle ──────────────────────────────────────────────────────────────
void uiReaderInit(lv_obj_t* parent) {
  s_root = lv_obj_create(parent);
  lv_obj_remove_style_all(s_root);
  lv_obj_set_size(s_root, SCR_W, SCR_H);
  lv_obj_set_style_bg_color(s_root, C_PLATE, 0);
  lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
  lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);

  // Back chip — same idiom as every other screen's return affordance.
  lv_obj_t* back = lv_btn_create(s_root);
  lv_obj_remove_style_all(back);
  lv_obj_set_size(back, 96, 32);
  lv_obj_set_pos(back, 16, 12);
  lv_obj_set_style_radius(back, R_MD, 0);
  lv_obj_set_style_bg_color(back, C_SURF_1, 0);
  lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
  uiPressable(back);
  lv_obj_add_event_cb(back, onBack, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* bl = lv_label_create(back);
  lv_obj_set_style_text_font(bl, F_MICRO, 0);
  lv_obj_set_style_text_letter_space(bl, 1, 0);
  lv_obj_set_style_text_color(bl, C_INK2, 0);
  lv_label_set_text(bl, "< NEWS");
  lv_obj_center(bl);

  s_meta = lv_label_create(s_root);
  lv_obj_set_style_text_font(s_meta, F_MICRO, 0);
  lv_obj_set_style_text_letter_space(s_meta, 1, 0);
  lv_obj_set_style_text_color(s_meta, C_INK3, 0);
  lv_obj_set_pos(s_meta, RD_COL_X, 22);
  lv_label_set_text(s_meta, "");

  s_headline = lv_label_create(s_root);
  lv_obj_set_style_text_font(s_headline, F_BODY, 0);
  lv_obj_set_style_text_color(s_headline, C_INK, 0);
  lv_obj_set_pos(s_headline, RD_COL_X, 50);
  lv_obj_set_width(s_headline, RD_COL_W);
  lv_obj_set_height(s_headline, 2 * (int)lv_font_get_line_height(F_BODY) + 4);
  lv_label_set_long_mode(s_headline, LV_LABEL_LONG_DOT);
  lv_label_set_text(s_headline, "");

  lv_obj_t* rule = lv_obj_create(s_root);
  lv_obj_remove_style_all(rule);
  lv_obj_set_pos(rule, RD_COL_X, RD_BODY_Y - 8);
  lv_obj_set_size(rule, RD_COL_W, 1);
  lv_obj_set_style_bg_color(rule, C_LINE, 0);
  lv_obj_set_style_bg_opa(rule, OPA_HAIR, 0);

  // The scroll band: full panel width so a drag starting in the margins still
  // moves the story; the column width comes from the label inside. The
  // SCROLLABLE / SCROLL_MOMENTUM / SCROLL_ELASTIC default flags are exactly
  // the behaviour asked for, so — unusually for this codebase — they are
  // deliberately NOT cleared.
  s_scroll = lv_obj_create(s_root);
  lv_obj_remove_style_all(s_scroll);
  lv_obj_set_pos(s_scroll, 0, RD_BODY_Y);
  lv_obj_set_size(s_scroll, SCR_W, SCR_H - RD_BODY_Y);
  lv_obj_set_scroll_dir(s_scroll, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(s_scroll, LV_SCROLLBAR_MODE_ACTIVE);
  lv_obj_set_style_pad_top(s_scroll, 4, 0);
  lv_obj_set_style_pad_bottom(s_scroll, 24, 0);
  // The scrollbar: a 4 px hairline that appears only while the story moves.
  lv_obj_set_style_bg_color(s_scroll, C_EDGE_HI, LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(s_scroll, 160, LV_PART_SCROLLBAR);
  lv_obj_set_style_width(s_scroll, 4, LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(s_scroll, R_XS, LV_PART_SCROLLBAR);
  lv_obj_set_style_pad_right(s_scroll, 6, LV_PART_SCROLLBAR);

  s_body = lv_label_create(s_scroll);
  lv_obj_set_style_text_font(s_body, F_BODY, 0);
  lv_obj_set_style_text_color(s_body, C_INK2, 0);
  lv_obj_set_style_text_line_space(s_body, 4, 0);
  lv_obj_set_pos(s_body, RD_COL_X, 0);
  lv_obj_set_width(s_body, RD_COL_W);
  lv_label_set_long_mode(s_body, LV_LABEL_LONG_WRAP);
  lv_label_set_text(s_body, "");
}

void uiReaderOpen(const NewsItem& it) {
  if (!s_root) return;
  s_item = it;
  metaLine();
  s_waiting = false;
  if (it.id[0] && apiStoryStart(it.id)) {
    s_waiting = true;
    lv_label_set_text(s_headline, it.headline);
    setBody("Loading the story…");
  } else {
    renderSummary(it.id[0] ? "(Couldn't start the fetch — summary only.)" : nullptr);
  }
  lv_obj_clear_flag(s_root, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(s_root);
}

void uiReaderTick() {
  if (!s_waiting || !g_storyReady) return;
  g_storyReady = false;
  s_waiting = false;
  // A stale completion for some other article must not clobber this one.
  if (strcmp(g_story.id, s_item.id) != 0) return;
  if (g_story.ok) renderStory();
  else            renderSummary("(The full story didn't load — summary only.)");
}

void uiReaderHide() {
  if (s_root) lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
  s_waiting = false;
}

bool uiReaderIsOpen() {
  return s_root && !lv_obj_has_flag(s_root, LV_OBJ_FLAG_HIDDEN);
}
