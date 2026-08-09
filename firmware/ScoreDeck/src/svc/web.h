// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
#pragma once

/** Start the setup portal and mDNS. Call once Wi-Fi is up. */
void webBegin();
/** Service pending requests. LOOP CONTEXT ONLY — handlers touch NVS and LVGL. */
void webLoop();
bool webUp();
