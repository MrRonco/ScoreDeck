#!/usr/bin/env node
// Embed portal/index.html into a PROGMEM header the firmware serves directly.
//
//   node tools/embed-portal.mjs
//
// Why a build step rather than a hand-written string: the portal is a real
// file that can be opened in a browser, linted and screenshotted against
// portal/mock.mjs. Keeping it authored as HTML and generated into C is what
// makes that possible — the alternative is editing markup inside C string
// escapes, which is where the escaping bugs live.
//
// The output is a plain raw string literal, not gzip. Gzip would save ~3x but
// costs a decompression path and a Content-Encoding header on a device that is
// already only serving one client; revisit if the page passes ~40 KB.
import { readFile, writeFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
const SRC = join(ROOT, 'portal/index.html');
const OUT = join(ROOT, 'firmware/ScoreDeck/src/svc/portal.h');

const html = await readFile(SRC, 'utf8');

// A raw literal cannot contain its own delimiter. )SDHTML" is not a sequence
// any HTML or JS would produce, but check rather than hope.
const DELIM = 'SDHTML';
if (html.includes(`)${DELIM}"`)) {
  console.error('portal contains the raw-string delimiter; pick another');
  process.exit(1);
}

const header = `// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
//
// GENERATED FILE — do not edit. Source: portal/index.html
// Regenerate with:  node tools/embed-portal.mjs
//
// Served straight from flash with sendContent_P, so it costs no heap. That
// matters more than it looks: the web server runs inside the same loop() that
// drives the display, so response time IS panel stall time, and building this
// page into a String first would put ${(html.length / 1024).toFixed(1)} KB through the allocator on
// every request.
#pragma once
#include <pgmspace.h>

static const char PORTAL_HTML[] PROGMEM = R"${DELIM}(${html})${DELIM}";
`;

await writeFile(OUT, header);
console.log(`portal.h  ${(html.length / 1024).toFixed(1)} KB`);
