// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
// lint_fonts.cpp — the guard for the bug that keeps coming back.
//
// The faces are generated with the narrowest glyph range each job needs, so a
// label pointed at the wrong face renders hollow boxes and says nothing about
// it. That shipped five times: "GOAL" on the alert card, "ScoreDeck" on setup,
// "3rd 04:21" on the game header, the lowercase slug in "STANDINGS nhl", and
// "1h 00m" on the idle countdown.
//
// Static analysis cannot catch it — the face is set where the object is built
// and the text arrives from upstream at runtime, often in another file. So we
// check the thing that actually matters: walk the live object tree and ask the
// font whether it can draw what the label is holding.
#include <cstdio>
#include <cstring>
#include "lint_fonts.h"
#include "../firmware/ScoreDeck/src/ui/theme.h"

static int s_missing;

static const char* faceName(const lv_font_t* f) {
  if (f == F_SCORE)     return "F_SCORE";
  if (f == F_SCORE_BIG) return "F_SCORE_BIG";
  if (f == F_DISPLAY)   return "F_DISPLAY";
  if (f == F_ABBR)      return "F_ABBR";
  if (f == F_BODY)      return "F_BODY";
  if (f == F_MICRO)     return "F_MICRO";
  return "?";
}

/** Render a codepoint for the report — printable ASCII as itself, else U+XXXX. */
static void describe(uint32_t cp, char* out, size_t cap) {
  if (cp >= 0x20 && cp < 0x7F) snprintf(out, cap, "'%c'", (char)cp);
  else                         snprintf(out, cap, "U+%04X", cp);
}

static void checkLabel(lv_obj_t* obj, const char* screen) {
  const char* txt = lv_label_get_text(obj);
  if (!txt || !*txt) return;
  const lv_font_t* font = lv_obj_get_style_text_font(obj, LV_PART_MAIN);
  if (!font) return;

  uint32_t i = 0;
  while (txt[i]) {
    const uint32_t cp = _lv_txt_encoded_next(txt, &i);
    if (!cp) break;
    lv_font_glyph_dsc_t dsc;
    if (lv_font_get_glyph_dsc(font, &dsc, cp, 0)) continue;
    char what[16];
    describe(cp, what, sizeof what);
    printf("  MISSING %-8s in %-11s  \"%s\"\n", what, faceName(font), txt);
    s_missing++;
    return;                       // one report per label is enough to fix it
  }
}

static void walk(lv_obj_t* obj, const char* screen) {
  if (!obj) return;
  if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return;   // not on screen, not our problem
  if (lv_obj_check_type(obj, &lv_label_class)) checkLabel(obj, screen);
  const uint32_t n = lv_obj_get_child_cnt(obj);
  for (uint32_t i = 0; i < n; i++) walk(lv_obj_get_child(obj, i), screen);
}

int lintScreen(const char* screen) {
  const int before = s_missing;
  printf("%s\n", screen);
  walk(lv_scr_act(), screen);
  if (s_missing == before) printf("  ok\n");
  return s_missing - before;
}

int lintTotal() { return s_missing; }
