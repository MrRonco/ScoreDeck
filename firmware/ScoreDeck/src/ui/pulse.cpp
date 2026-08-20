// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
// pulse.cpp — the live dots breathe. The only thing on this panel that moves.
//
// Grepping the UI for lv_anim, lv_timer_create, lv_canvas, lv_arc, img_recolor,
// bg_img_src and lv_draw_mask returned ZERO files across 4,700 lines. Nothing
// had ever moved. That is most of what "seems basic" means on an emissive
// display: the panel was behaving like a printed page.
//
// THE BUDGET, and why it is a band and not a pixel count.
//
// The draw buffer is 800x30 lines (hal_display.cpp), so one slice is 24,000 px
// / 48 KB and any invalidated region inside a single 30-line band flushes in
// exactly one flush_cb. A full 800x480 repaint measures ~230 ms, which implies
// ~3.3 MB/s of effective write throughput against the panel's own read DMA —
// so a single tick must stay under roughly 100 KB, or ~50,000 px, to avoid
// competing with the 30 ms refresh cadence.
//
// Nine dots at 6x6 is 324 px per frame. That is 1.35% of ONE band, three
// orders of magnitude inside the budget, and it is the cheapest possible thing
// that makes the board read as live rather than as a screenshot.
//
// What must NOT animate, for the same arithmetic: a board crossfade is 384,000
// px and would also overflow LV_INV_BUF_SIZE (64) into a forced full-screen
// repaint; anything carrying glassPanel()'s specular children re-runs their
// size-changed callback every frame; and the idle countdown is an 11,000 px
// glyph run on the screen the panel spends most of its life showing.
#include "ui.h"
#include "theme.h"

#define PULSE_MAX   20          // 12 grid tiles + hero + hero foot + game sheet + headroom
#define PULSE_MS    100         // tick period; the cycle is built from steps
#define PULSE_STEPS 20          // 2.0 s round trip

static lv_obj_t*  s_dot[PULSE_MAX];
static uint8_t    s_count;
static lv_timer_t* s_timer;
static uint8_t    s_step;

// Discrete, not a tween. UI.md §8 requires every fade on this panel to be a
// small number of fixed steps, because a smooth ramp is indistinguishable at
// 610 mm and costs an invalidation per frame to produce it.
// SOLID COLOURS, not an opacity ramp. bg_opa blends toward whatever is
// behind the dot: lv_color_mix quantises opacity to (opa + 4) >> 3 — 26
// levels, not 256 — and pulls chroma down with lightness, so the dot at its
// trough rendered as a dimmer, greener teal than the flat accent elsewhere
// on the panel and measured 3.79:1 on the hero surface, below AA. These five
// rungs are pre-solved on the ramp toward A_LIVE: even 1.2-1.5 L* steps, a
// 5.4 L* amplitude (still plainly a breath at 610 mm), and every rung is
// >= 7.39:1 on every surface it can sit on.
static lv_color_t stepColour(uint8_t step) {
  static const lv_color_t kRung[5] = { A_LIVE_P0, A_LIVE_P1, A_LIVE_P2,
                                       A_LIVE_P3, A_LIVE_P4 };
  const int half = PULSE_STEPS / 2;
  const int up = step < half ? step : PULSE_STEPS - step;   // triangle
  int i = up * 4 / half;                                    // 0..4
  if (i > 4) i = 4;
  return kRung[i];
}

static void tick(lv_timer_t*) {
  s_step = (uint8_t)((s_step + 1) % PULSE_STEPS);
  const lv_color_t o = stepColour(s_step);
  for (uint8_t i = 0; i < s_count; i++) {
    lv_obj_t* d = s_dot[i];
    if (!d) continue;
    // Hidden dots are skipped rather than written: an invisible object still
    // invalidates its area when a style changes, and on a board with one live
    // game that would be eleven pointless invalidations per tick.
    if (lv_obj_has_flag(d, LV_OBJ_FLAG_HIDDEN)) continue;
    lv_obj_set_style_bg_color(d, o, 0);
  }
}

void pulseRegister(lv_obj_t* dot) {
  if (!dot || s_count >= PULSE_MAX) return;
  s_dot[s_count++] = dot;
  // ONE timer for every dot on the panel. One timer per dot would multiply the
  // wakeups by twelve to produce identical output.
  if (!s_timer) s_timer = lv_timer_create(tick, PULSE_MS, nullptr);
}

void pulseForget() {
  // uiInit() deletes and rebuilds the whole board on a density change, which
  // leaves every registered pointer dangling. Called from there, before the
  // rebuild, so the timer never walks freed objects.
  s_count = 0;
}
