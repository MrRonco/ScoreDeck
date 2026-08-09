#!/usr/bin/env node
// Build team logo blobs for your own proxy.
//
// NOTHING THIS PRODUCES MAY BE COMMITTED. The outputs are derivative works of
// ESPN-hosted assets carrying league and club trademarks — see
// docs/OPEN_SOURCE.md §1. assets/logos/ is gitignored; this runs on your
// machine, into your own Worker.
//
//   npm i -g sharp   (or run from proxy/ where it is a devDependency)
//   node tools/build-logos.mjs nhl nfl nba mlb
//
// Output: assets/logos/<league>/<ABBR>@<size>.bin
//   LVGL LV_IMG_CF_TRUE_COLOR_ALPHA, 16-bit colour => 3 bytes/px
//   header is LVGL's 4-byte lv_img_header_t
import { mkdir, writeFile } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..', '..');
const SIZES = [48, 96];
const BASE = 'https://site.api.espn.com/apis/site/v2/sports';

const PATHS = {
  nhl: 'hockey/nhl', nfl: 'football/nfl', nba: 'basketball/nba',
  mlb: 'baseball/mlb', wnba: 'basketball/wnba', ncaaf: 'football/college-football',
  ncaam: 'basketball/mens-college-basketball', ncaaw: 'basketball/womens-college-basketball',
  'eng.1': 'soccer/eng.1', 'esp.1': 'soccer/esp.1', 'ger.1': 'soccer/ger.1',
  'ita.1': 'soccer/ita.1', 'fra.1': 'soccer/fra.1', ucl: 'soccer/uefa.champions',
  mls: 'soccer/usa.1', nwsl: 'soccer/usa.nwsl',
};

/** LVGL true-colour-alpha at 16bpp: RGB565 little-endian, then one alpha byte. */
function toLvgl(raw, w, h) {
  const header = Buffer.alloc(4);
  // cf = 5 (LV_IMG_CF_TRUE_COLOR_ALPHA), always_zero = 0, reserved = 0
  header.writeUInt32LE((5 & 0x1f) | ((w & 0x7ff) << 10) | ((h & 0x7ff) << 21), 0);
  const out = Buffer.alloc(w * h * 3);
  for (let i = 0, o = 0; i < w * h; i++) {
    const r = raw[i * 4], g = raw[i * 4 + 1], b = raw[i * 4 + 2], a = raw[i * 4 + 3];
    const px = ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
    out[o++] = px & 0xff;
    out[o++] = px >> 8;
    out[o++] = a;
  }
  return Buffer.concat([header, out]);
}

async function build(slug) {
  const path = PATHS[slug];
  if (!path) { console.error(`  unknown league ${slug}`); return; }
  const { default: sharp } = await import('sharp');

  const res = await fetch(`${BASE}/${path}/teams`, { headers: { accept: 'application/json' } });
  if (!res.ok) { console.error(`  ${slug}: teams ${res.status}`); return; }
  const json = await res.json();
  const teams = (json?.sports?.[0]?.leagues?.[0]?.teams ?? []).map((t) => t.team).filter(Boolean);

  let ok = 0, miss = 0;
  for (const t of teams) {
    const abbr = t.abbreviation;
    const url = t.logos?.[0]?.href;
    if (!abbr || !url) { miss++; continue; }
    try {
      const img = await fetch(url);
      if (!img.ok) { miss++; continue; }
      const buf = Buffer.from(await img.arrayBuffer());
      for (const size of SIZES) {
        const { data, info } = await sharp(buf)
          .resize(size, size, { fit: 'contain', background: { r: 0, g: 0, b: 0, alpha: 0 } })
          .ensureAlpha()
          .raw()
          .toBuffer({ resolveWithObject: true });
        const dir = join(ROOT, 'assets', 'logos', slug);
        await mkdir(dir, { recursive: true });
        await writeFile(join(dir, `${abbr}@${size}.bin`), toLvgl(data, info.width, info.height));
      }
      ok++;
    } catch {
      miss++;
    }
  }
  console.log(`  ${slug}: ${ok} teams built, ${miss} skipped`);
}

const args = process.argv.slice(2);
if (!args.length) {
  console.error('usage: node tools/build-logos.mjs <league> [league...]');
  console.error('leagues:', Object.keys(PATHS).join(' '));
  process.exit(1);
}
console.log('Building logo blobs. These are trademarked assets — never commit them.');
for (const a of args) await build(a);
console.log(`\nDone. Serve assets/logos/ from your proxy; the device fetches
/v1/logo/<league>/<ABBR>@48.bin and falls back to the colour badge on a miss.`);
