// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
#pragma once
#include <lvgl.h>

/** Set when a fetch lands, so the board repaints once rather than every tick. */
extern volatile bool g_logoArrived;

/** Cached logo for a team, or nullptr — draw the colour badge instead. */
const lv_img_dsc_t* logoGet(const char* league, const char* abbr);
/** True once we have an answer, hit or 404. Stops us asking forever. */
bool logoKnown(const char* league, const char* abbr);
bool logoRequest(const char* league, const char* abbr);
/** Fetch at most one missing on-screen logo. Loop context only. */
void logoTick();
