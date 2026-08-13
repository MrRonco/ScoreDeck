// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
// ui_rail.cpp — the hideable league rail. refresh-spec.md §9.
//
// Day-to-day FILTERING lives here — what the header chips used to do, with
// room to breathe. Enabling/disabling leagues is a different job (catalog,
// cap meter, AUTO explanations) and stays in settings; the two were split
// because the cap's recovery text alone is 264 px wide and cannot survive a
// 140 px rail.
//
// TWO STATES, NO SLIDE. Open/close is a discrete swap — the board rebuilds
// through the same one-shot path a density change already uses (~230 ms full
// repaint). A tweened slide of a 140x432 panel would cost more than the
// panel's entire per-tick throughput; banned, and not missed: on glass a
// snap reads as decisive.
//
// THE COLLAPSED SLIVER IS ONE CANVAS. A proportional spine — one segment per
// league with games tonight, height by its share, C_LIVE when live — drawn
// into a single 16x432 lv_canvas. Twelve separate segment objects would add
// twelve invalidation regions to every poll and re-break exactly what the
// t.sit reposition fixed; a canvas invalidates as ONE region no matter how
// many rects are drawn into it (verified against lv_canvas.c — every draw
// invalidates the whole canvas, and LVGL dedupes identical rectangles).
#include "ui.h"
#include "theme.h"
#include "../config.h"
#include "../core/state.h"
#include <esp_heap_caps.h>

#define RAIL_W      140
#define SLIVER_W     16
#define RAIL_TOP     48                 // below the bar
#define RAIL_H      (SCR_H - RAIL_TOP)
#define RAIL_ROW_H   29
#define RAIL_MAX_ROWS (MAX_LEAGUES + 1) // ALL + every payload league

static bool      s_open;                // survives uiInit() rebuilds
static lv_obj_t* s_root;                // expanded rail
static lv_obj_t* s_sliver;              // collapsed canvas
static lv_obj_t* s_overlay;             // swallows the outside tap
static lv_obj_t* s_rowBg[RAIL_MAX_ROWS];
static lv_obj_t* s_rowTab[RAIL_MAX_ROWS];
static lv_obj_t* s_rowName[RAIL_MAX_ROWS];
static lv_obj_t* s_rowCount[RAIL_MAX_ROWS];
static lv_obj_t* s_rowUnder[RAIL_MAX_ROWS];

// The frozen order: captured at open, so rows never re-rank under a finger.
// Row 0 is ALL; rows 1.. map to these league indices.
static int8_t    s_order[MAX_LEAGUES];
static uint8_t   s_orderN;

// The canvas buffer is allocated once and survives rebuilds; the canvas
// OBJECT is recreated with the board (it is a child of it).
static lv_color_t* s_cbuf;

lv_obj_t* uiRailRoot() { return s_root; }
bool uiRailOpen() { return s_open; }

// ── the sliver ─────────────────────────────────────────────────────────────
static void sliverPaint() {
  if (!s_sliver || !s_cbuf) return;
  lv_canvas_fill_bg(s_sliver, C_SURF_1, LV_OPA_COVER);

  // Total games across leagues with any game tonight.
  uint16_t total = 0;
  for (uint8_t i = 0; i < g_leagueCount; i++) {
    uint8_t n = 0;
    for (uint8_t k = 0; k < g_gameCount; k++)
      if (strcmp(g_board[k].league, g_leagues[i].slug) == 0) n++;
    total += n;
  }
  if (!total) return;

  lv_draw_rect_dsc_t seg;
  lv_draw_rect_dsc_init(&seg);
  seg.radius = 2;

  int y = 6;
  const int usable = RAIL_H - 12;
  for (uint8_t i = 0; i < g_leagueCount && y < RAIL_H - 8; i++) {
    uint8_t n = 0;
    for (uint8_t k = 0; k < g_gameCount; k++)
      if (strcmp(g_board[k].league, g_leagues[i].slug) == 0) n++;
    if (!n) continue;
    int h = usable * n / total - 2;
    if (h < 6) h = 6;
    seg.bg_color = g_leagues[i].live ? C_LIVE : C_EDGE_HI;
    lv_canvas_draw_rect(s_sliver, 4, y, 8, h, &seg);
    // The white tab: which league the FILTER currently is.
    if (g_leagueFilter == (int8_t)i) {
      lv_draw_rect_dsc_t tab;
      lv_draw_rect_dsc_init(&tab);
      tab.bg_color = lv_color_white();
      tab.radius = 1;
      lv_canvas_draw_rect(s_sliver, 0, y, 3, h < 12 ? h : 12, &tab);
    }
    y += h + 4;
  }
}

// ── open / close ───────────────────────────────────────────────────────────
static void onSliver(lv_event_t*) { uiRailToggle(); }
static void onOverlay(lv_event_t*) { uiRailToggle(); }   // swallow + close

static void rowApply(uint8_t r);

static void onRow(lv_event_t* e) {
  const int r = (int)(intptr_t)lv_event_get_user_data(e);
  // Row 0 is ALL; others resolve through the frozen order.
  g_leagueFilter = (r == 0) ? -1 : s_order[r - 1];
  g_page = 0;
  // Close-on-select: the choice IS the dismissal. A timer was rejected — it
  // fires a 230 ms full-screen repaint while nobody is looking, with nothing
  // on screen to attribute it to.
  uiRailToggle();
}

static void onEdit(lv_event_t*) {
  s_open = false;                 // settings covers the screen; come back closed
  uiSettingsOpen();
}

void uiRailToggle() {
  s_open = !s_open;
  // Freeze the order at the moment of OPENING — not in uiRailInit, which
  // reruns on every rebuild, including poll-triggered auto-density ones.
  // "Rows never re-rank under a finger" has to survive those too.
  if (s_open) {
    s_orderN = 0;
    for (uint8_t i = 0; i < g_leagueCount && s_orderN < MAX_LEAGUES; i++)
      s_order[s_orderN++] = (int8_t)i;
  }
  // The board rebuilds for the new geometry through the same guard a density
  // change uses; uiBoardRefresh() sees the state change and calls uiInit().
  uiInit();
  uiBoardRefresh();
}

// ── build ──────────────────────────────────────────────────────────────────
static void rowApply(uint8_t r) {
  const bool isAll = (r == 0);
  const int8_t li = isAll ? -1 : s_order[r - 1];
  const bool sel = (g_leagueFilter == li);

  uint8_t live = 0, total = 0;
  if (isAll) {
    for (uint8_t k = 0; k < g_gameCount; k++) {
      total++;
      if (g_board[k].state == GS_LIVE) live++;
    }
  } else if (li >= 0 && li < g_leagueCount) {
    for (uint8_t k = 0; k < g_gameCount; k++)
      if (strcmp(g_board[k].league, g_leagues[li].slug) == 0) {
        total++;
        if (g_board[k].state == GS_LIVE) live++;
      }
  }

  lv_obj_set_style_bg_opa(s_rowBg[r], sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  sel ? lv_obj_clear_flag(s_rowTab[r], LV_OBJ_FLAG_HIDDEN)
      : lv_obj_add_flag(s_rowTab[r], LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_text_color(s_rowName[r],
      sel ? C_INK : (live ? C_INK2 : C_INK3), 0);

  char buf[12];
  if (total) snprintf(buf, sizeof buf, "%u /%u", live, total);
  else       snprintf(buf, sizeof buf, "-");
  lv_label_set_text(s_rowCount[r], buf);
  lv_obj_set_style_text_color(s_rowCount[r],
      live ? C_LIVE : (total ? C_INK3 : lv_color_hex(0x4A5666)), 0);
  (live && !sel) ? lv_obj_clear_flag(s_rowUnder[r], LV_OBJ_FLAG_HIDDEN)
                 : lv_obj_add_flag(s_rowUnder[r], LV_OBJ_FLAG_HIDDEN);
}

void uiRailInit(lv_obj_t* parent) {
  // The frozen order is captured in uiRailToggle() at the open transition;
  // capturing here would silently re-rank on any poll that rebuilds the
  // board while the rail is up. If the rail somehow opens without a capture
  // (boot state is closed, so this is belt-and-braces), take one now.
  if (s_open && s_orderN == 0)
    for (uint8_t i = 0; i < g_leagueCount && s_orderN < MAX_LEAGUES; i++)
      s_order[s_orderN++] = (int8_t)i;

  if (!s_open) {
    s_root = nullptr;
    s_overlay = nullptr;
    // The collapsed sliver. ext_click_area stretches the 16 px strip to the
    // 24 px WCAG floor; the bleed lands in the first tile's padding.
    if (!s_cbuf)
      s_cbuf = (lv_color_t*)heap_caps_malloc(
          LV_CANVAS_BUF_SIZE_TRUE_COLOR(SLIVER_W, RAIL_H), MALLOC_CAP_SPIRAM);
    if (!s_cbuf) { s_sliver = nullptr; return; }
    s_sliver = lv_canvas_create(parent);
    lv_canvas_set_buffer(s_sliver, s_cbuf, SLIVER_W, RAIL_H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(s_sliver, 0, RAIL_TOP);
    lv_obj_add_flag(s_sliver, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_sliver, 8);
    lv_obj_add_event_cb(s_sliver, onSliver, LV_EVENT_CLICKED, nullptr);
    sliverPaint();
    return;
  }

  s_sliver = nullptr;

  // The outside-tap catcher, UNDER the rail in z-order but over the board.
  // It must swallow the tap: without it the first tap after opening would
  // both close the rail and open whatever game happened to be underneath.
  s_overlay = lv_obj_create(parent);
  lv_obj_remove_style_all(s_overlay);
  lv_obj_set_pos(s_overlay, 0, 0);
  lv_obj_set_size(s_overlay, SCR_W, SCR_H);
  lv_obj_set_style_bg_opa(s_overlay, LV_OPA_TRANSP, 0);
  lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(s_overlay, onOverlay, LV_EVENT_CLICKED, nullptr);

  s_root = lv_obj_create(parent);
  lv_obj_remove_style_all(s_root);
  lv_obj_set_pos(s_root, 0, RAIL_TOP);
  lv_obj_set_size(s_root, RAIL_W, RAIL_H);
  lv_obj_set_style_bg_color(s_root, C_SURF_1, 0);
  lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
  lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* edge = lv_obj_create(s_root);
  lv_obj_remove_style_all(edge);
  lv_obj_set_pos(edge, RAIL_W - 1, 0);
  lv_obj_set_size(edge, 1, RAIL_H);
  lv_obj_set_style_bg_color(edge, C_LINE, 0);
  lv_obj_set_style_bg_opa(edge, 26, 0);

  const uint8_t rows = (uint8_t)(1 + s_orderN);
  for (uint8_t r = 0; r < rows && r < RAIL_MAX_ROWS; r++) {
    const int y = 8 + r * RAIL_ROW_H;
    s_rowBg[r] = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_rowBg[r]);
    lv_obj_set_pos(s_rowBg[r], 6, y - 3);
    lv_obj_set_size(s_rowBg[r], RAIL_W - 12, 26);
    lv_obj_set_style_radius(s_rowBg[r], 6, 0);
    lv_obj_set_style_bg_color(s_rowBg[r], C_EDGE_HI, 0);
    lv_obj_set_style_bg_opa(s_rowBg[r], LV_OPA_TRANSP, 0);
    lv_obj_add_flag(s_rowBg[r], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_rowBg[r], onRow, LV_EVENT_CLICKED, (void*)(intptr_t)r);

    s_rowTab[r] = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_rowTab[r]);
    lv_obj_set_pos(s_rowTab[r], 0, y - 3);
    lv_obj_set_size(s_rowTab[r], 3, 26);
    lv_obj_set_style_bg_color(s_rowTab[r], lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_rowTab[r], LV_OPA_COVER, 0);
    lv_obj_add_flag(s_rowTab[r], LV_OBJ_FLAG_HIDDEN);

    s_rowName[r] = lv_label_create(s_root);
    lv_obj_set_pos(s_rowName[r], 14, y);
    lv_obj_set_style_text_font(s_rowName[r], F_MICRO, 0);
    lv_obj_set_style_text_letter_space(s_rowName[r], 1, 0);
    if (r == 0) {
      lv_label_set_text(s_rowName[r], "ALL");
    } else {
      char up[10];
      const char* slug = g_leagues[s_order[r - 1]].slug;
      size_t j = 0;
      for (; slug[j] && j < sizeof up - 1; j++) up[j] = (char)toupper((unsigned char)slug[j]);
      up[j] = '\0';
      lv_label_set_text(s_rowName[r], up);
    }

    s_rowCount[r] = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_rowCount[r], F_MICRO, 0);
    lv_obj_set_width(s_rowCount[r], 54);
    lv_obj_set_style_text_align(s_rowCount[r], LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(s_rowCount[r], 76, y);
    lv_label_set_text(s_rowCount[r], "");

    s_rowUnder[r] = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_rowUnder[r]);
    lv_obj_set_pos(s_rowUnder[r], 14, y + 15);
    lv_obj_set_size(s_rowUnder[r], 30, 2);
    lv_obj_set_style_radius(s_rowUnder[r], 1, 0);
    lv_obj_set_style_bg_color(s_rowUnder[r], C_LIVE, 0);
    lv_obj_set_style_bg_opa(s_rowUnder[r], LV_OPA_COVER, 0);
    lv_obj_add_flag(s_rowUnder[r], LV_OBJ_FLAG_HIDDEN);

    rowApply(r);
  }

  // EDIT SPORTS — a deep link, not the feature. The pane arrives in the next
  // iteration; until then this opens settings, which is where it will live.
  lv_obj_t* rule = lv_obj_create(s_root);
  lv_obj_remove_style_all(rule);
  lv_obj_set_pos(rule, 8, RAIL_H - 34);
  lv_obj_set_size(rule, RAIL_W - 16, 1);
  lv_obj_set_style_bg_color(rule, C_LINE, 0);
  lv_obj_set_style_bg_opa(rule, OPA_HAIR, 0);
  lv_obj_t* edit = lv_label_create(s_root);
  lv_obj_set_pos(edit, 14, RAIL_H - 26);
  lv_obj_set_style_text_font(edit, F_MICRO, 0);
  lv_obj_set_style_text_letter_space(edit, 1, 0);
  lv_obj_set_style_text_color(edit, C_INK3, 0);
  lv_label_set_text(edit, "EDIT SPORTS  >");
  lv_obj_add_flag(edit, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(edit, 8);
  lv_obj_add_event_cb(edit, onEdit, LV_EVENT_CLICKED, nullptr);
}

void uiRailRefresh() {
  if (s_open && s_root) {
    // Counts refresh in place; positions are frozen until close.
    const uint8_t rows = (uint8_t)(1 + s_orderN);
    for (uint8_t r = 0; r < rows && r < RAIL_MAX_ROWS; r++) rowApply(r);
  } else if (s_sliver) {
    sliverPaint();
  }
}
