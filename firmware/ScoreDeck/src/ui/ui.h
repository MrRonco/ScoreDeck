// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
#pragma once
#include <lvgl.h>

enum Screen : uint8_t { SCR_BOARD = 0, SCR_IDLE, SCR_SETUP };

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
/** Root of the setup screen, or nullptr before uiSetupInit(). */
lv_obj_t* uiSetupRoot();

// Idle — what the panel shows when nothing is live. UI.md §7.
void uiIdleInit(lv_obj_t* parent);
void uiIdleRefresh();          // on new data
void uiIdleTick();             // once a second: clock + countdown
lv_obj_t* uiIdleRoot();
/** True when no game on the board is live. */
bool uiShouldIdle();
