// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
#pragma once
enum { SCN_TYPICAL = 0, SCN_EMPTY, SCN_ALL_LIVE, SCN_EXTREMES,
       SCN_CROWDED, SCN_NO_PROXY, SCN_STALE, SCN_ACCENTS, SCN_FIELD, SCN_COUNT };
void        scenarioApply(int n);
void        scenarioReapply(int n);
const char* scenarioName(int n);
