// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
// theme.h — palette and font tokens.
//
// ScoreDeck has NO accent colour. ~3,000 team colours arrive as content, so the
// chrome stays strictly neutral and every saturated pixel belongs to a team.
// Luminance encodes state; colour encodes team. UI.md §2.
#pragma once
#include <lvgl.h>

#define C_VOID    lv_color_hex(0x05070C)
#define C_PLATE   lv_color_hex(0x0A0F18)
#define C_FROST   lv_color_hex(0x1A2432)
#define C_FROST_2 lv_color_hex(0x141C28)
#define C_INK     lv_color_hex(0xF3F7FB)
#define C_INK2    lv_color_hex(0x93A5B8)
#define C_INK3    lv_color_hex(0x5D6D7E)
#define C_EDGE    lv_color_hex(0x2A3646)
#define C_EDGE_HI lv_color_hex(0x46566A)

// Luminance by game state — the state channel (UI.md §2).
#define OPA_LIVE  LV_OPA_COVER
#define OPA_PRE   ((lv_opa_t)184)   // 72%
#define OPA_FINAL ((lv_opa_t)140)   // 55%

extern const lv_font_t* F_SCORE;   // tabular, the big numerals
extern const lv_font_t* F_ABBR;
extern const lv_font_t* F_BODY;
extern const lv_font_t* F_MICRO;

void themeInit();

/** Frosted panel style — the baked-glass primitive. */
lv_obj_t* glassPanel(lv_obj_t* parent, int x, int y, int w, int h, int radius);

/** Team colour glyph used when no logo blob is present (always, for now). */
lv_obj_t* teamBadge(lv_obj_t* parent, const char* abbr, uint32_t color, int size);
