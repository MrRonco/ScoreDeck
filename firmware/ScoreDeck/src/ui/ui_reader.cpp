// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
// ui_reader.cpp — the article reader. UI.md §5.4.
//
// PAGED, with the interaction the thumb already makes. Three designs deep:
// tap zones on the outer thirds were undiscoverable (the owner dragged, and
// nothing moved); true drag-to-scroll moved ~180k px per frame and redrew at
// a jank the owner rightly refused. This version keeps the crisp one-shot
// page repaint and accepts BOTH inputs everywhere — tap anywhere in the text
// band or swipe up for the next page, swipe down for the previous one — with
// a reading-progress thumb on the right edge, a page counter, and a one-time
// hint on the first page so nothing has to be taught twice.
//
// The body sits in a centred 560 px column — ~70 characters of F_BODY per
// line, book measure. Page breaks are found by binary search over
// lv_txt_get_size, so the paginator and the renderer agree by construction.
//
// The reader is an OVERLAY above the news list, not a Screen enum member:
// uiCurrent() stays SCR_NEWS while it is up, so the main loop's
// never-navigate-away guard (which already protects news) covers it with no
// new plumbing, and back simply unhides the list beneath.
//
// That overlay used to be paid for with an OPAQUE C_PLATE root — the only
// screen root on the panel that was. Measured on a settled --screen reader
// frame: 353,012 of 384,000 px (91.9 %) were the single value (0,4,8) and the
// whole frame carried 61 distinct colours, against 206 on idle and 309 on the
// board. It was not the plate; it was a flat repaint OF the plate that hid
// plate.cpp's Bayer dither entirely (1 distinct colour in the x 0-99 /
// y 150-469 strip). The root is transparent now and the list beneath is
// hidden explicitly instead, so the article sits on the same ground and in
// the same glass as everything else the product draws.
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
// The column the text sits ON. 20 px either side of the measure, which puts
// its left edge on 100 and its right on 700 — 100 px of plate down each
// margin, the only place the dither is allowed to be seen.
#define RD_PAD     20
#define RD_PANEL_X (RD_COL_X - RD_PAD)
#define RD_PANEL_W (RD_COL_W + 2 * RD_PAD)
#define RD_PANEL_Y 120
// 324, not 340: the body band gives up 16 px (300 -> 284, ~0.6 of a line at
// F_BODY + 4) so the card clears the one-time hint at 456 by the 12 px gutter
// instead of colliding with it. One extra page on a nine-paragraph story.
#define RD_PANEL_H 324
#define RD_BODY_Y  (RD_PANEL_Y + RD_PAD)
#define RD_BODY_H  (RD_PANEL_H - 2 * RD_PAD)
#define RD_MAX_PG    8
#define RD_TRACK_X (SCR_W - 12)
#define RD_TRACK_H RD_PANEL_H

static lv_obj_t* s_root;
static lv_obj_t* s_headline;
static lv_obj_t* s_meta;
static lv_obj_t* s_count;          // "2/4" — how much is left, exactly
static lv_obj_t* s_body;
static lv_obj_t* s_track;          // reading-progress rail…
static lv_obj_t* s_thumb;          // …and where in the story you are
static lv_obj_t* s_hint;           // first-page-only "TAP OR SWIPE UP"
static lv_obj_t* s_zone;           // the whole text band accepts input

static NewsItem s_item;            // the article being read (copied — the
                                   // feed can refresh underneath us)
static char*    s_text;            // the buffer the current pages index into
static uint16_t s_pgOff[RD_MAX_PG + 1];
static uint8_t  s_pgN;
static uint8_t  s_pg;
static bool     s_hinted;          // hint dismissed for this article
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

static void showPage() {
  if (!s_text) return;
  const uint16_t a = s_pgOff[s_pg], b = s_pgOff[s_pg + 1];
  const char saved = s_text[b];
  s_text[b] = '\0';
  lv_label_set_text(s_body, s_text + a);   // set_text copies; restore is safe
  s_text[b] = saved;

  const bool multi = s_pgN > 1;
  char c[8];
  snprintf(c, sizeof c, "%u/%u", (unsigned)(s_pg + 1), (unsigned)s_pgN);
  lv_label_set_text(s_count, multi ? c : "");

  // The rail: thumb position = how far in, thumb size = how much one page is.
  if (multi) {
    lv_obj_clear_flag(s_track, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_thumb, LV_OBJ_FLAG_HIDDEN);
    int th = RD_TRACK_H / s_pgN;
    if (th < 24) th = 24;
    const int run = RD_TRACK_H - th;
    lv_obj_set_height(s_thumb, th);
    lv_obj_set_y(s_thumb, RD_PANEL_Y + (s_pgN < 2 ? 0 : run * s_pg / (s_pgN - 1)));
  } else {
    lv_obj_add_flag(s_track, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_thumb, LV_OBJ_FLAG_HIDDEN);
  }

  // The hint earns its pixels exactly once per article.
  if (multi && !s_hinted && s_pg == 0) lv_obj_clear_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
  else                                 lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
}

// ── content states ─────────────────────────────────────────────────────────
static void renderStory() {
  lv_label_set_text(s_headline,
                    g_story.headline[0] ? g_story.headline : s_item.headline);
  s_text = g_story.body;
  paginate(s_text);
  s_pg = 0;
  showPage();
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
  showPage();
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

static void pageStep(bool fwd) {
  if (s_waiting || !s_text) return;
  if (fwd && s_pg + 1 < s_pgN) s_pg++;
  else if (!fwd && s_pg > 0)   s_pg--;
  else return;
  s_hinted = true;
  showPage();
}

static void onTap(lv_event_t*) { pageStep(true); }
static void onGesture(lv_event_t*) {
  lv_indev_t* in = lv_indev_get_act();
  const lv_dir_t d = lv_indev_get_gesture_dir(in);
  if (d != LV_DIR_TOP && d != LV_DIR_BOTTOM) return;
  // A gesture does NOT cancel the press in LVGL — without this, the release
  // still fired SHORT_CLICKED, so every swipe down paged back and instantly
  // paged forward again ("it keeps going down"). Swallow the rest of the
  // press the moment the swipe is recognised.
  lv_indev_wait_release(in);
  pageStep(d == LV_DIR_TOP);   // up: read on · down: back up
}

// ── lifecycle ──────────────────────────────────────────────────────────────
void uiReaderInit(lv_obj_t* parent) {
  s_root = lv_obj_create(parent);
  lv_obj_remove_style_all(s_root);
  lv_obj_set_size(s_root, SCR_W, SCR_H);
  // Transparent, exactly as ui_news.cpp says its own root is and for the same
  // reason: painting a flat C_PLATE here hides the very thing plate.cpp
  // exists to draw. uiReaderOpen() hides the list underneath instead.
  lv_obj_set_style_bg_color(s_root, C_PLATE, 0);
  lv_obj_set_style_bg_opa(s_root, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);

  // The standard top bar: bare plate with one hairline, NOT a glassPanel.
  // ui_board.cpp:713 argues the case and ui_idle.cpp:133 repeats it — frost
  // puts chrome on the live-tile rung, and here it would also have made the
  // bar and the article card the same 38,400 px of one colour.
  lv_obj_t* bar = lv_obj_create(s_root);
  lv_obj_remove_style_all(bar);
  lv_obj_set_pos(bar, 0, 0);
  lv_obj_set_size(bar, SCR_W, BAR_H);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* barLine = lv_obj_create(bar);
  lv_obj_remove_style_all(barLine);
  lv_obj_set_pos(barLine, 0, BAR_H - 1);
  lv_obj_set_size(barLine, SCR_W, 1);
  lv_obj_set_style_bg_color(barLine, C_LINE, 0);
  lv_obj_set_style_bg_opa(barLine, OPA_HAIR, 0);
  // One baseline for everything in the strip, from the face's own metrics.
  const int hdrY = (BAR_H - (int)lv_font_get_line_height(F_MICRO)) / 2;

  // Back chip. This site was the idiom the other five were measured against —
  // it is now backChip() itself, so "same idiom" is enforced rather than
  // asserted. The only thing that changes here is the width: 96 px was hand
  // set, and the chip is content-sized around its own word.
  backChip(bar, "NEWS", onBack);

  s_meta = lv_label_create(bar);
  lv_obj_set_style_text_font(s_meta, F_MICRO, 0);
  lv_obj_set_style_text_letter_space(s_meta, 1, 0);
  lv_obj_set_style_text_color(s_meta, C_INK3, 0);
  lv_obj_set_pos(s_meta, 128, hdrY);
  lv_label_set_text(s_meta, "");

  // "2/4", top right — the answer to "how much is left" in two glyphs.
  s_count = lv_label_create(bar);
  lv_obj_set_style_text_font(s_count, F_MICRO, 0);
  lv_obj_set_style_text_letter_space(s_count, 1, 0);
  lv_obj_set_style_text_color(s_count, C_INK3, 0);
  lv_obj_set_style_text_align(s_count, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_width(s_count, 64);
  lv_obj_set_pos(s_count, SCR_W - 16 - 64, hdrY);
  lv_label_set_text(s_count, "");

  // The headline stays ON the plate, above the card: 17.22:1 at its worst
  // background there, against 14.75:1 inside the card. It also keeps the
  // headline and the body distinguishable now that both are C_INK — the
  // surface under them is doing the work the ink tier used to do.
  s_headline = lv_label_create(s_root);
  lv_obj_set_style_text_font(s_headline, F_BODY, 0);
  lv_obj_set_style_text_color(s_headline, C_INK, 0);
  lv_obj_set_pos(s_headline, RD_COL_X, BAR_H + 12);
  lv_obj_set_width(s_headline, RD_COL_W);
  lv_obj_set_height(s_headline, 2 * (int)lv_font_get_line_height(F_BODY) + 4);
  lv_label_set_long_mode(s_headline, LV_LABEL_LONG_DOT);
  lv_label_set_text(s_headline, "");

  // The article's card. This replaces the hairline rule that used to be the
  // only thing separating headline from body: a card edge says it better, and
  // says the article is made of the same glass as every other screen.
  glassPanel(s_root, RD_PANEL_X, RD_PANEL_Y, RD_PANEL_W, RD_PANEL_H, R_XL);

  s_body = lv_label_create(s_root);
  lv_obj_set_style_text_font(s_body, F_BODY, 0);
  // C_INK, not C_INK2. The prose moved off the plate and onto C_SURF_2, and
  // ink2 renders 8.26:1 there against the 10.81:1 it measured on the ground —
  // a regression on the longest read in the product. C_INK on the same glass
  // measures 14.75:1 (AA-masked, rendered), so the card costs the body
  // nothing and pays it 3.94 back.
  lv_obj_set_style_text_color(s_body, C_INK, 0);
  lv_obj_set_style_text_line_space(s_body, 4, 0);
  lv_obj_set_pos(s_body, RD_COL_X, RD_BODY_Y);
  // One line-height taller than the paginator's budget: a page whose last
  // line lands exactly on the boundary keeps its descenders.
  lv_obj_set_size(s_body, RD_COL_W,
                  RD_BODY_H + (int)lv_font_get_line_height(F_BODY));
  lv_label_set_long_mode(s_body, LV_LABEL_LONG_WRAP);
  lv_label_set_text(s_body, "");

  // Reading progress, on the right edge where every scrollbar ever lived.
  s_track = lv_obj_create(s_root);
  lv_obj_remove_style_all(s_track);
  lv_obj_set_pos(s_track, RD_TRACK_X, RD_PANEL_Y);
  lv_obj_set_size(s_track, 4, RD_TRACK_H);
  lv_obj_set_style_radius(s_track, R_XS, 0);
  lv_obj_set_style_bg_color(s_track, C_EDGE_HI, 0);
  lv_obj_set_style_bg_opa(s_track, 70, 0);
  lv_obj_add_flag(s_track, LV_OBJ_FLAG_HIDDEN);
  s_thumb = lv_obj_create(s_root);
  lv_obj_remove_style_all(s_thumb);
  lv_obj_set_pos(s_thumb, RD_TRACK_X, RD_PANEL_Y);
  lv_obj_set_size(s_thumb, 4, 40);
  lv_obj_set_style_radius(s_thumb, R_XS, 0);
  lv_obj_set_style_bg_color(s_thumb, C_INK3, 0);
  lv_obj_set_style_bg_opa(s_thumb, LV_OPA_COVER, 0);
  lv_obj_add_flag(s_thumb, LV_OBJ_FLAG_HIDDEN);

  s_hint = lv_label_create(s_root);
  lv_obj_set_style_text_font(s_hint, F_MICRO, 0);
  lv_obj_set_style_text_letter_space(s_hint, 1, 0);
  lv_obj_set_style_text_color(s_hint, C_INK3, 0);
  lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(s_hint, 300);
  lv_obj_set_pos(s_hint, (SCR_W - 300) / 2, 456);
  lv_label_set_text(s_hint, "TAP OR SWIPE UP FOR MORE");
  lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);

  // ONE input surface: the whole band below the header. A tap anywhere turns
  // the page; swipes go both ways. Nothing to discover, nothing to miss.
  s_zone = lv_obj_create(s_root);
  lv_obj_remove_style_all(s_zone);
  lv_obj_set_pos(s_zone, 0, BAR_H);
  lv_obj_set_size(s_zone, SCR_W, SCR_H - BAR_H);
  uiTapZone(s_zone);   // the page turning IS the feedback; see uiTapZone()
  lv_obj_clear_flag(s_zone, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(s_zone, onTap, LV_EVENT_SHORT_CLICKED, nullptr);
  lv_obj_add_event_cb(s_zone, onGesture, LV_EVENT_GESTURE, nullptr);
}

void uiReaderOpen(const NewsItem& it) {
  if (!s_root) return;
  s_item = it;
  metaLine();
  s_pg = 0;
  s_hinted = false;
  s_waiting = false;
  if (it.id[0] && apiStoryStart(it.id)) {
    s_waiting = true;
    s_text = nullptr;
    lv_label_set_text(s_headline, it.headline);
    lv_label_set_text(s_body, "Loading the story…");
    lv_label_set_text(s_count, "");
    lv_obj_add_flag(s_track, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_thumb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
  } else {
    renderSummary(it.id[0] ? "(Couldn't start the fetch — summary only.)" : nullptr);
  }
  // The root is transparent now, so the list has to be put away explicitly —
  // otherwise five news cards read straight through the article.
  if (lv_obj_t* nw = uiNewsRoot()) lv_obj_add_flag(nw, LV_OBJ_FLAG_HIDDEN);
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
  // Back to the list — but only if the list is still where we are. Today
  // uiNewsClose() calls this one line before uiShow() puts the board up, so
  // the guard changes nothing; it is here so this function is right on its
  // own rather than by luck about its caller's next statement.
  lv_obj_t* nw = uiNewsRoot();
  if (nw && uiCurrent() == SCR_NEWS) lv_obj_clear_flag(nw, LV_OBJ_FLAG_HIDDEN);
  s_waiting = false;
}

bool uiReaderIsOpen() {
  return s_root && !lv_obj_has_flag(s_root, LV_OBJ_FLAG_HIDDEN);
}
