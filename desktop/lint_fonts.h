// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
#pragma once
#include <lvgl.h>

/** Walk the visible object tree and report labels their font cannot draw.
 *  Returns the number of offending labels found on this screen. */
int lintScreen(const char* screen);

/** Total across every lintScreen() call so far — the process exit code. */
int lintTotal();
