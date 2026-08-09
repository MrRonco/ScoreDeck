// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
#pragma once
#include <lvgl.h>

enum Screen : uint8_t { SCR_BOARD = 0, SCR_SETUP };

void uiInit();
/** Rebuild from g_board. Loop context only. Every write is change-cached. */
void uiBoardRefresh();
void uiShow(Screen s);
Screen uiCurrent();

// Top bar
void uiSetClock(const char* hhmm, const char* date);
void uiSetStatus();

// Setup / onboarding
void uiSetupInit(lv_obj_t* parent);
bool uiSetupActive();
