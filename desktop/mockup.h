// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
#pragma once

/** Draw a proposed screen over everything else. Harness only — mockup.cpp
 *  touches nothing in firmware/, so a rejected design costs one deleted file. */
void        mockupApply(int n);
const char* mockupName(int n);
#define MOCK_COUNT 13
