// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
// spike.h — one-off capability spikes for the design-envelope document.
// Not part of the product; measures real LVGL 8.3 costs on this display
// stack so phase-2 design decisions are made against measured numbers.
#pragma once

/** Run every spike in sequence, printing measured px/frame and ms/frame to
 *  stdout. Requires themeInit()/plateInit()/bloomInit() to already have run. */
void spikeRun();

/** Same BMP writer main.cpp uses for --shot. Shared so spike.cpp can drop
 *  visual-proof frames next to the measured numbers. */
bool writeBmp(const char* path);
