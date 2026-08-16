// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
// ui_reader.cpp — the article reader. UI.md §5.4.
//
// PAGED, never scrolled. This panel repaints a full screen in ~230 ms as a
// one-shot, which is perfect for tap-to-turn and hopeless for continuous
// scrolling; a page turn is one honest repaint. The body sits in a centred
// 560 px column — ~70 characters of F_BODY per line, book measure, instead
// of 100+ character full-bleed lines nobody can track back across.
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
#define RD_BODY_Y  128
#define RD_BODY_H  300
#define RD_MAX_PG    8

static lv_obj_t* s_root;
static lv_obj_t* s_headline;
static lv_obj_t* s_meta;
static lv_obj_t* s_body;
static lv_obj_t* s_dots[RD_MAX_PG];
static lv_obj_t* s_zoneL;
static lv_obj_t* s_zoneR;

static NewsItem s_item;            // the article being read (copied — the
                                   // feed can refresh underneath us)
static char*    s_text;            // the buffer the current pages index into
static uint16_t s_pgOff[RD_MAX_PG + 1];
static uint8_t  s_pgN;
static uint8_t  s_pg;
static bool     s_waiting;         // story fetch in flight for s_item

// ── pagination ─────────────────────────────────────────────────────────────
/** Longest prefix of text+off that wraps into RD_BODY_H at RD_COL_W.
 *  Binary search over the character count; every probe is a full LVGL wrap
 *  layout via lv_txt_get_size, so the breaks agree with the renderer by
 *  construction. Cuts land on whitespace, never mid-word. */
static uint16_t pageFit(char* text, uint16_t off) {
  const uint16_t rest = (uint16_t)strlen(text + off);
  if (!rest) return 0;
  lv_point_t sz;
  uint16_t lo = 1, hi = rest, best = 1;
  while (lo <= hi) {
    uint16_t mid = (uint16_t)((lo + hi) / 2);
    // Back up to a breakable point so the probe never splits a word.
    uint16_t cut = mid;
    if (cut < rest) {
      while (cut > 1 && text[off + cut] != ' ' && text[off + cut] != '\n') cut--;
      if (cut <= 1) cut = mid;         // one giant token — split it anyway
    }
    const char saved = text[off + cut];
    text[off + cut] = '\0';
    lv_txt_get_size(&sz, text + off, F_BODY, 0, 4, RD_COL_W, LV_TEXT_FLAG_NONE);
    text[off + cut] = saved;
    if (sz.y <= RD_BODY_H) { best = cut; lo = (uint16_t)(mid + 1); }
    else                   { hi = (uint16_t)(mid - 1); }
  }
  // Swallow the whitespace the cut landed on so pages never open blank.
  uint16_t adv = best;
  while (text[off + adv] == ' ' || text[off + adv] == '\n') adv++;
  return adv;
}

static void paginate(char* text) {
  s_pgN = 0;
  uint16_t off = 0;
  while (text[off] && s_pgN < RD_MAX_PG) {
    s_pgOff[s_pgN++] = off;
    const uint16_t used = pageFit(text, off);
    if (!used) break;
    off = (uint16_t)(off + used);
  }
  s_pgOff[s_pgN] = off;               // terminal offset of the last page
  if (!s_pgN) { s_pgOff[0] = 0; s_pgOff[1] = 0; s_pgN = 1; }
}

static void showPage(char* text) {
  const uint16_t a = s_pgOff[s_pg], b = s_pgOff[s_pg + 1];
  const char saved = text[b];
  text[b] = '\0';
  lv_label_set_text(s_body, text + a);   // set_text copies; restore is safe
  text[b] = saved;
  for (uint8_t i = 0; i < RD_MAX_PG; i++) {
    if (i >= s_pgN || s_pgN < 2) { lv_obj_add_flag(s_dots[i], LV_OBJ_FLAG_HIDDEN); continue; }
    lv_obj_clear_flag(s_dots[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(s_dots[i], SCR_W / 2 - (s_pgN * 14 - 8) / 2 + i * 14, 456);
    lv_obj_set_style_bg_color(s_dots[i], i == s_pg ? C_INK : C_EDGE_HI, 0);
  }
}

// ── content states ─────────────────────────────────────────────────────────
static void renderStory() {
  lv_label_set_text(s_headline,
                    g_story.headline[0] ? g_story.headline : s_item.headline);
  s_text = g_story.body;
  paginate(s_text);
  s_pg = 0;
  showPage(s_text);
}

/** No body (no id, premium, or the fetch failed): the summary is the story. */
static void renderSummary(const char* note) {
  lv_label_set_text(s_headline, s_item.headline);
  static char buf[300];
  snprintf(buf, sizeof buf, "%s%s%s", s_item.desc,
           note ? "\n\n" : "", note ? note : "");
  s_text = buf;
  paginate(s_text);
  s_pg = 0;
  showPage(s_text);
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
static void onZone(lv_event_t* e) {
  const bool fwd = (bool)(intptr_t)lv_event_get_user_data(e);
  if (s_waiting) return;
  if (!s_text) return;
  if (fwd && s_pg + 1 < s_pgN) s_pg++;
  else if (!fwd && s_pg > 0)   s_pg--;
  else return;
  showPage(s_text);
}

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
  lv_obj_set_pos(s_headline, RD_COL_X, 52);
  lv_obj_set_width(s_headline, RD_COL_W);
  lv_obj_set_height(s_headline, 2 * (int)lv_font_get_line_height(F_BODY) + 4);
  lv_label_set_long_mode(s_headline, LV_LABEL_LONG_DOT);
  lv_label_set_text(s_headline, "");

  lv_obj_t* rule = lv_obj_create(s_root);
  lv_obj_remove_style_all(rule);
  lv_obj_set_pos(rule, RD_COL_X, RD_BODY_Y - 12);
  lv_obj_set_size(rule, RD_COL_W, 1);
  lv_obj_set_style_bg_color(rule, C_LINE, 0);
  lv_obj_set_style_bg_opa(rule, OPA_HAIR, 0);

  s_body = lv_label_create(s_root);
  lv_obj_set_style_text_font(s_body, F_BODY, 0);
  lv_obj_set_style_text_color(s_body, C_INK2, 0);
  lv_obj_set_style_text_line_space(s_body, 4, 0);
  lv_obj_set_pos(s_body, RD_COL_X, RD_BODY_Y);
  // One line-height taller than the paginator's budget: a page whose last
  // line lands exactly on the boundary keeps its descenders.
  lv_obj_set_size(s_body, RD_COL_W,
                  RD_BODY_H + (int)lv_font_get_line_height(F_BODY));
  // WRAP, not CLIP: in v8 CLIP keeps each paragraph on one unwrapped line.
  // Wrapping matches lv_txt_get_size's layout, so the paginator's breaks and
  // the renderer agree; the fixed height only ever clips the last part-line.
  lv_label_set_long_mode(s_body, LV_LABEL_LONG_WRAP);
  lv_label_set_text(s_body, "");

  for (uint8_t i = 0; i < RD_MAX_PG; i++) {
    s_dots[i] = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_dots[i]);
    lv_obj_set_size(s_dots[i], 6, 6);
    lv_obj_set_style_radius(s_dots[i], 3, 0);
    lv_obj_set_style_bg_opa(s_dots[i], LV_OPA_COVER, 0);
    lv_obj_add_flag(s_dots[i], LV_OBJ_FLAG_HIDDEN);
  }

  // Page-turn zones: the outer thirds, below the header. Invisible, generous.
  for (int k = 0; k < 2; k++) {
    lv_obj_t* z = lv_obj_create(s_root);
    lv_obj_remove_style_all(z);
    lv_obj_set_size(z, 240, SCR_H - 100);
    lv_obj_set_pos(z, k ? SCR_W - 240 : 0, 100);
    lv_obj_add_flag(z, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(z, onZone, LV_EVENT_SHORT_CLICKED, (void*)(intptr_t)k);
    if (k) s_zoneR = z; else s_zoneL = z;
  }
}

void uiReaderOpen(const NewsItem& it) {
  if (!s_root) return;
  s_item = it;
  metaLine();
  s_pg = 0;
  s_waiting = false;
  if (it.id[0] && apiStoryStart(it.id)) {
    s_waiting = true;
    lv_label_set_text(s_headline, it.headline);
    lv_label_set_text(s_body, "Loading the story…");
    for (uint8_t i = 0; i < RD_MAX_PG; i++) lv_obj_add_flag(s_dots[i], LV_OBJ_FLAG_HIDDEN);
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
