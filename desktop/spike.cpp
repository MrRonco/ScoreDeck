// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
// spike.cpp — measured, not guessed. See spike.h.
//
// Cost model matches the one the firmware already uses (pulse.cpp, plate.cpp):
// pixels touched per redraw, checked against the documented ~50,000 px/tick
// / ~3.3 MB/s budget from INHERITED_RULES.md and UI.md. Wall-clock ms is also
// printed but is a desktop Apple-Silicon number, not an ESP32-S3 one — do not
// quote it as a device timing, only the relative ordering and the px counts
// are portable.
#include <lvgl.h>
#include <cstdio>
#include <chrono>
#include <thread>
#include "spike.h"
#include "../firmware/ScoreDeck/src/config.h"
#include "../firmware/ScoreDeck/src/ui/ui.h"
#include "../firmware/ScoreDeck/src/ui/theme.h"

// Bumped by main.cpp's flush_cb every spike run.
extern volatile uint32_t g_spikePx;
extern volatile uint32_t g_spikeFlushN;
extern volatile int32_t  g_spikeX1, g_spikeY1, g_spikeX2, g_spikeY2;

using Clock = std::chrono::high_resolution_clock;

static void resetCounters() { g_spikePx = 0; g_spikeFlushN = 0; }

static double msSince(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

/** One redraw: mutate, refresh, measure. Runs `warmup` untimed passes first
 *  so the FIRST draw (object creation, one-time layout) never pollutes what
 *  is supposed to be a steady-state per-frame number. */
static void measure(const char* label, int warmup, void (*mutate)(int i)) {
  for (int i = 0; i < warmup; i++) { mutate(i); lv_refr_now(nullptr); }
  resetCounters();
  const auto t0 = Clock::now();
  mutate(warmup);
  lv_refr_now(nullptr);
  const double ms = msSince(t0);
  printf("%-38s  %6u px  %5u flush%s  %6.3f ms  bbox %ld,%ld-%ld,%ld (%ldx%ld)\n",
         label, g_spikePx, g_spikeFlushN, g_spikeFlushN == 1 ? "" : "es", ms,
         (long)g_spikeX1, (long)g_spikeY1, (long)g_spikeX2, (long)g_spikeY2,
         (long)(g_spikeX2 - g_spikeX1 + 1), (long)(g_spikeY2 - g_spikeY1 + 1));
}

// ── 1. circular clip of an image (radius mask) ─────────────────────────────
// A dedicated NATIVE 96x96 sprite (not a zoomed-down 220px one) — see the
// gotcha documented in spikeCircleClipZoomGotcha() below for why the
// distinction matters.
static uint8_t s_circleBuf[96 * 96 * 3];
static void spikeCircleClip() {
  lv_obj_t* scr = lv_scr_act();
  for (int y = 0; y < 96; y++) for (int x = 0; x < 96; x++) {
    uint8_t* p = s_circleBuf + (y * 96 + x) * 3;
    p[0] = 0xFF; p[1] = 0xFF; p[2] = 0xFF;
  }
  static lv_img_dsc_t dsc;
  dsc.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
  dsc.header.always_zero = 0;
  dsc.header.w = 96; dsc.header.h = 96;
  dsc.data_size = sizeof(s_circleBuf);
  dsc.data = s_circleBuf;

  lv_obj_t* img = lv_img_create(scr);
  lv_img_set_src(img, &dsc);
  lv_obj_set_pos(img, 40, 40);
  lv_obj_set_style_radius(img, 48, 0);           // 96px native -> full circle
  lv_obj_set_style_clip_corner(img, true, 0);
  lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, 0);
  lv_obj_set_style_img_recolor(img, lv_color_hex(0x3BE0C0), 0);
  lv_refr_now(nullptr);
  writeBmp("shots/spike-circle-clip.bmp");

  static uint32_t c = 0;
  measure("circle-clip NATIVE 96x96 image, recolour", 1, [](int) {
    c ^= 1;
    lv_obj_t* o = lv_obj_get_child(lv_scr_act(), lv_obj_get_child_cnt(lv_scr_act()) - 1);
    lv_obj_set_style_img_recolor(o, lv_color_hex(c ? 0xF2B441 : 0x3BE0C0), 0);
  });
  lv_obj_del(img);
}

// ── 1b. THE GOTCHA — recolour on a ZOOMED-DOWN image ────────────────────────
// bloomCreate() always builds the FINAL 220x220 sprite (bloom.cpp: no zoom/
// pivot/size-mode support in that helper), zoomed here to ~96px on screen —
// exactly what a design would do to reuse one big soft sprite at many sizes.
// lv_img_set_angle()/set_zoom() invalidate the true POST-transform footprint
// (proven above by the badge-rotate spike: 64x64, not 220x220+pad) — but a
// plain STYLE change (recolour, opacity) on that same object does NOT: it
// goes through the generic lv_obj_refresh_style() path, which invalidates the
// object's BASE, PRE-ZOOM coordinate box. A recolour on a badge that reads
// ~96px on screen costs as if the badge were still 220px.
static void spikeCircleClipZoomGotcha() {
  lv_obj_t* scr = lv_scr_act();
  lv_obj_t* img = bloomCreate(scr, 96, 96);
  if (!img) { printf("circle-clip zoomed-badge (gotcha)      SKIPPED (no bloom sprite)\n"); return; }
  lv_obj_set_pos(img, 40, 40);
  lv_img_set_zoom(img, (uint16_t)((256 * 96) / 220));   // 220px sprite drawn at ~96px
  lv_img_set_pivot(img, 110, 110);
  lv_obj_set_style_radius(img, 48, 0);
  lv_obj_set_style_clip_corner(img, true, 0);
  bloomSet(img, 0x3BE0C0, LV_OPA_COVER);
  lv_refr_now(nullptr);

  static uint32_t c = 0;
  measure("circle-clip ZOOMED 220->96px, recolour", 1, [](int) {
    c ^= 1;
    lv_obj_set_style_img_recolor(lv_obj_get_child(lv_scr_act(), lv_obj_get_child_cnt(lv_scr_act()) - 1),
                                 lv_color_hex(c ? 0xF2B441 : 0x3BE0C0), 0);
  });
  lv_obj_del(img);
}

// ── 2. rotated image (transform_angle), static then "slowly spinning" ──────
static void spikeRotateImage() {
  lv_obj_t* scr = lv_scr_act();
  lv_obj_t* img = bloomCreate(scr, 220, 220);
  if (!img) { printf("rotate-image 220x220                   SKIPPED (no bloom sprite)\n"); return; }
  lv_obj_set_pos(img, 300, 130);
  bloomSet(img, 0x3BE0C0, LV_OPA_COVER);
  lv_img_set_pivot(img, 110, 110);
  lv_img_set_angle(img, 0);
  lv_refr_now(nullptr);

  static int16_t a = 0;
  measure("rotate 220x220 image, 1 degree step", 1, [](int) {
    a = (int16_t)((a + 10) % 3600);
    lv_img_set_angle(lv_obj_get_child(lv_scr_act(), lv_obj_get_child_cnt(lv_scr_act()) - 1), a);
  });

  // A realistic small badge (48x48-ish zoomed) instead of the 220px bloom.
  lv_obj_t* img2 = bloomCreate(scr, 220, 220);
  lv_obj_set_pos(img2, 300, 130);
  bloomSet(img2, 0x3BE0C0, LV_OPA_COVER);
  lv_img_set_zoom(img2, (256 * 48) / 220);       // shrink the 220px sprite to ~48px on screen
  lv_img_set_pivot(img2, 110, 110);
  lv_refr_now(nullptr);
  static int16_t b = 0;
  measure("rotate ~48px badge (zoomed), 1 deg step", 1, [](int) {
    b = (int16_t)((b + 10) % 3600);
    lv_img_set_angle(lv_obj_get_child(lv_scr_act(), lv_obj_get_child_cnt(lv_scr_act()) - 1), b);
  });
  lv_img_set_angle(img2, 450);   // 45 deg, so the shot proves rotation happened
  lv_refr_now(nullptr);
  writeBmp("shots/spike-rotate.bmp");

  lv_obj_del(img);
  lv_obj_del(img2);
}

// ── 3. lv_arc — radial gauge redraw cost ────────────────────────────────────
static void spikeArc() {
  lv_obj_t* scr = lv_scr_act();
  lv_obj_t* arc = lv_arc_create(scr);
  lv_obj_set_size(arc, 120, 120);
  lv_obj_set_pos(arc, 40, 40);
  lv_arc_set_rotation(arc, 270);
  lv_arc_set_bg_angles(arc, 0, 360);
  lv_arc_set_range(arc, 0, 100);
  lv_obj_set_style_arc_width(arc, 10, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, 10, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, lv_color_hex(0x2A3646), LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, lv_color_hex(0x3BE0C0), LV_PART_INDICATOR);
  lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
  lv_arc_set_value(arc, 40);
  lv_refr_now(nullptr);
  writeBmp("shots/spike-arc.bmp");

  static int v = 40;
  measure("lv_arc 120x120, 10px ring, +1 value", 1, [](int) {
    v = (v + 1) % 100;
    lv_arc_set_value(lv_obj_get_child(lv_scr_act(), lv_obj_get_child_cnt(lv_scr_act()) - 1), v);
  });
  lv_obj_del(arc);
}

// ── 4. lv_chart — sparkline / win-prob line redraw cost ─────────────────────
static void spikeChart() {
  lv_obj_t* scr = lv_scr_act();
  lv_obj_t* chart = lv_chart_create(scr);
  lv_obj_set_size(chart, 200, 40);
  lv_obj_set_pos(chart, 40, 200);
  lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(chart, 30);
  lv_chart_set_div_line_count(chart, 0, 0);
  lv_obj_set_style_size(chart, 0, LV_PART_INDICATOR);   // no point markers
  lv_chart_series_t* s = lv_chart_add_series(chart, lv_color_hex(0x3BE0C0), LV_CHART_AXIS_PRIMARY_Y);
  for (int i = 0; i < 30; i++) lv_chart_set_next_value(chart, s, 50 + (i * 7) % 40);
  lv_refr_now(nullptr);
  writeBmp("shots/spike-chart.bmp");

  measure("lv_chart 200x40 line, 30pt, push value", 1, [](int i) {
    lv_obj_t* c = lv_obj_get_child(lv_scr_act(), lv_obj_get_child_cnt(lv_scr_act()) - 1);
    lv_chart_series_t* ser = lv_chart_get_series_next(c, nullptr);
    lv_chart_set_next_value(c, ser, 30 + (i * 13) % 60);
  });
  lv_obj_del(chart);
}

// ── 5. lv_canvas — hand-drawn sparkline (rects) vs lv_chart ─────────────────
static uint8_t s_canvasBuf[LV_CANVAS_BUF_SIZE_TRUE_COLOR(200, 40)];
static void spikeCanvas() {
  lv_obj_t* scr = lv_scr_act();
  lv_obj_t* cv = lv_canvas_create(scr);
  lv_canvas_set_buffer(cv, s_canvasBuf, 200, 40, LV_IMG_CF_TRUE_COLOR);
  lv_obj_set_pos(cv, 260, 200);
  lv_canvas_fill_bg(cv, lv_color_hex(0x101825), LV_OPA_COVER);
  lv_draw_rect_dsc_t rdsc0;
  lv_draw_rect_dsc_init(&rdsc0);
  rdsc0.bg_color = lv_color_hex(0x3BE0C0);
  rdsc0.bg_opa = LV_OPA_COVER;
  for (int b = 0; b < 20; b++) {
    const int h = 4 + ((b * 7) % 34);
    lv_canvas_draw_rect(cv, b * 10, 40 - h, 8, h, &rdsc0);
  }
  lv_refr_now(nullptr);
  writeBmp("shots/spike-canvas.bmp");

  measure("lv_canvas 200x40, full redraw (20 bars)", 1, [](int i) {
    lv_obj_t* c = lv_obj_get_child(lv_scr_act(), lv_obj_get_child_cnt(lv_scr_act()) - 1);
    lv_canvas_fill_bg(c, lv_color_hex(0x101825), LV_OPA_COVER);
    lv_draw_rect_dsc_t rd; lv_draw_rect_dsc_init(&rd);
    rd.bg_color = lv_color_hex(0x3BE0C0); rd.bg_opa = LV_OPA_COVER;
    for (int b = 0; b < 20; b++) {
      const int h = 4 + ((b * 7 + i) % 34);
      lv_canvas_draw_rect(c, b * 10, 40 - h, 8, h, &rd);
    }
    lv_obj_invalidate(c);
  });
  lv_obj_del(cv);
}

// ── 6. marquee / ticker — LONG_SCROLL cost per frame ────────────────────────
static void spikeMarquee() {
  lv_obj_t* scr = lv_scr_act();
  lv_obj_t* l = lv_label_create(scr);
  lv_obj_set_style_text_font(l, F_BODY, 0);
  lv_obj_set_style_text_color(l, C_INK, 0);
  lv_obj_set_width(l, 300);
  lv_obj_set_pos(l, 40, 300);
  lv_label_set_long_mode(l, LV_LABEL_LONG_SCROLL);
  lv_label_set_text(l, "BREAKING: this is a long ticker headline that does not fit in three hundred pixels at all");
  lv_refr_now(nullptr);

  // LV_LABEL_LONG_SCROLL moves distance proportional to REAL elapsed ms since
  // the animation started (millis()-driven, LV_TICK_CUSTOM), not a fixed
  // per-call step — so lv_anim_refr_now() alone (near-zero elapsed time)
  // correctly measured 0 px. Sleep one panel refresh period (30 ms,
  // LV_DISP_DEF_REFR_PERIOD) between steps to reproduce what actually happens
  // between two lv_timer_handler() calls on the device.
  for (int i = 0; i < 3; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    lv_anim_refr_now();
    lv_refr_now(nullptr);
  }
  resetCounters();
  const auto t0 = Clock::now();
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  lv_anim_refr_now();
  lv_refr_now(nullptr);
  const double ms = msSince(t0);
  printf("%-38s  %6u px  %5u flush%s  %6.3f ms  bbox %ld,%ld-%ld,%ld (%ldx%ld)\n",
         "marquee 300px wide, one 30ms step", g_spikePx, g_spikeFlushN,
         g_spikeFlushN == 1 ? "" : "es", ms,
         (long)g_spikeX1, (long)g_spikeY1, (long)g_spikeX2, (long)g_spikeY2,
         (long)(g_spikeX2 - g_spikeX1 + 1), (long)(g_spikeY2 - g_spikeY1 + 1));
  lv_obj_del(l);
}

// ── 7. diagonal line mask (parallelogram / angled cut) ──────────────────────
static lv_draw_mask_line_param_t s_lineMask;
static int16_t s_lineMaskId = -1;

static void lineMaskBegin(lv_event_t* e) {
  lv_obj_t* o = lv_event_get_target(e);
  lv_area_t a; lv_obj_get_coords(o, &a);
  lv_draw_mask_line_points_init(&s_lineMask, a.x1, a.y2, a.x2, a.y1,
                                LV_DRAW_MASK_LINE_SIDE_LEFT);
  s_lineMaskId = lv_draw_mask_add(&s_lineMask, nullptr);
}
static void lineMaskEnd(lv_event_t*) {
  if (s_lineMaskId >= 0) { lv_draw_mask_remove_id(s_lineMaskId); s_lineMaskId = -1; }
}

static void spikeLineMask() {
  lv_obj_t* scr = lv_scr_act();
  lv_obj_t* panel = lv_obj_create(scr);
  lv_obj_remove_style_all(panel);
  lv_obj_set_size(panel, 200, 100);
  lv_obj_set_pos(panel, 560, 40);
  lv_obj_set_style_bg_color(panel, lv_color_hex(0x1B2636), 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
  lv_obj_add_event_cb(panel, lineMaskBegin, LV_EVENT_DRAW_MAIN_BEGIN, nullptr);
  lv_obj_add_event_cb(panel, lineMaskEnd, LV_EVENT_DRAW_MAIN_END, nullptr);
  lv_refr_now(nullptr);

  writeBmp("shots/spike-line-mask.bmp");
  measure("200x100 panel, diagonal line mask, full redraw", 1, [](int) {
    lv_obj_t* p = lv_obj_get_child(lv_scr_act(), lv_obj_get_child_cnt(lv_scr_act()) - 1);
    lv_obj_invalidate(p);
  });
  lv_obj_del(panel);
}

// ── 8. angle (pie) mask — reveal wedge over a filled rect ───────────────────
static lv_draw_mask_angle_param_t s_angleMask;
static int16_t s_angleMaskId = -1;
static int16_t s_angleEnd = 90;

static void angleMaskBegin(lv_event_t* e) {
  lv_obj_t* o = lv_event_get_target(e);
  lv_area_t a; lv_obj_get_coords(o, &a);
  const int cx = (a.x1 + a.x2) / 2, cy = (a.y1 + a.y2) / 2;
  lv_draw_mask_angle_init(&s_angleMask, cx, cy, 270, (uint16_t)(270 + s_angleEnd) % 360);
  s_angleMaskId = lv_draw_mask_add(&s_angleMask, nullptr);
}
static void angleMaskEnd(lv_event_t*) {
  if (s_angleMaskId >= 0) { lv_draw_mask_remove_id(s_angleMaskId); s_angleMaskId = -1; }
}

static void spikeAngleMask() {
  lv_obj_t* scr = lv_scr_act();
  lv_obj_t* panel = lv_obj_create(scr);
  lv_obj_remove_style_all(panel);
  lv_obj_set_size(panel, 120, 120);
  lv_obj_set_pos(panel, 200, 40);
  lv_obj_set_style_bg_color(panel, lv_color_hex(0x3BE0C0), 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
  lv_obj_add_event_cb(panel, angleMaskBegin, LV_EVENT_DRAW_MAIN_BEGIN, nullptr);
  lv_obj_add_event_cb(panel, angleMaskEnd, LV_EVENT_DRAW_MAIN_END, nullptr);
  lv_refr_now(nullptr);

  measure("120x120 panel, angle-mask wedge reveal step", 1, [](int) {
    s_angleEnd = (int16_t)((s_angleEnd + 12) % 360);
    lv_obj_t* p = lv_obj_get_child(lv_scr_act(), lv_obj_get_child_cnt(lv_scr_act()) - 1);
    lv_obj_invalidate(p);
  });
  writeBmp("shots/spike-angle-mask.bmp");
  lv_obj_del(panel);
}

// ── 9. split-flap: two labels + clip, one flips ──────────────────────────────
static void spikeSplitFlap() {
  lv_obj_t* scr = lv_scr_act();
  lv_obj_t* top = lv_label_create(scr);
  lv_obj_set_style_text_font(top, F_SCORE, 0);
  lv_obj_set_style_text_color(top, C_INK, 0);
  lv_obj_set_pos(top, 700, 300);
  lv_obj_set_size(top, 40, 22);
  lv_label_set_long_mode(top, LV_LABEL_LONG_CLIP);
  lv_label_set_text(top, "7");
  lv_refr_now(nullptr);

  static int n = 7;
  measure("split-flap digit relabel (38px score face)", 1, [](int) {
    n = (n + 1) % 10;
    char buf[2] = { (char)('0' + n), 0 };
    lv_label_set_text(lv_obj_get_child(lv_scr_act(), lv_obj_get_child_cnt(lv_scr_act()) - 1), buf);
  });
  lv_obj_del(top);
}

void spikeRun() {
  printf("\n=== SPIKE MEASUREMENTS (desktop harness, LVGL 8.3.11, real theme/fonts) ===\n");
  printf("px = pixels flushed for that ONE redraw; compare to the ~50,000 px/tick\n");
  printf("budget documented in pulse.cpp. ms is Apple-Silicon desktop time, NOT the\n");
  printf("ESP32-S3 — relative ordering and px counts are what transfers.\n\n");
  spikeCircleClip();
  spikeCircleClipZoomGotcha();
  spikeRotateImage();
  spikeArc();
  spikeChart();
  spikeCanvas();
  spikeMarquee();
  spikeLineMask();
  spikeAngleMask();
  spikeSplitFlap();
  printf("\n=== END SPIKE ===\n\n");
}
