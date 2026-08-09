#!/usr/bin/env node
// Build player headshot blobs for your own proxy.
//
// NOTHING THIS PRODUCES MAY BE COMMITTED. Headshots are PERSONAL LIKENESSES,
// licensed through the players' associations — a stricter category than team
// marks. assets/players/ is gitignored and CI fails if a blob appears there.
// See docs/OPEN_SOURCE.md §1.
//
//   npm run headshots -- nhl:21 mlb:14        (league:teamId, your favourites)
//
// Source images are 230-280 KB and ESPN does no CDN-side resizing (?w= is
// ignored), so all the work happens here: 68x68 RGB565A8 = 13.9 KB each.
import { mkdir, writeFile, access } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..', '..');
const SIZE = 68;
const BASE = 'https://site.api.espn.com/apis/site/v2/sports';

const PATHS = {
  nhl: 'hockey/nhl', nfl: 'football/nfl', nba: 'basketball/nba',
  mlb: 'baseball/mlb', wnba: 'basketball/wnba',
};
const CDN = { nhl: 'nhl', nfl: 'nfl', nba: 'nba', mlb: 'mlb', wnba: 'wnba' };

function toLvgl(raw, w, h) {
  const header = Buffer.alloc(4);
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

async function buildTeam(slug, teamId) {
  const path = PATHS[slug];
  if (!path) { console.error(`  unknown league ${slug}`); return; }
  const { default: sharp } = await import('sharp');

  const res = await fetch(`${BASE}/${path}/teams/${teamId}/roster`, {
    headers: { accept: 'application/json' },
  });
  if (!res.ok) { console.error(`  ${slug}:${teamId} roster ${res.status}`); return; }
  const json = await res.json();

  // Roster shape differs by sport: flat `athletes`, or grouped with `items`.
  const groups = json?.athletes ?? [];
  const players = groups.flatMap((g) => (Array.isArray(g?.items) ? g.items : [g])).filter((p) => p?.id);

  const dir = join(ROOT, 'assets', 'players', slug);
  await mkdir(dir, { recursive: true });

  let ok = 0, skip = 0, had = 0;
  for (const p of players) {
    const out = join(dir, `${p.id}.bin`);
    try { await access(out); had++; continue; } catch { /* not built yet */ }
    const url = p?.headshot?.href ?? `https://a.espncdn.com/i/headshots/${CDN[slug]}/players/full/${p.id}.png`;
    try {
      const img = await fetch(url);
      if (!img.ok) { skip++; continue; }
      const buf = Buffer.from(await img.arrayBuffer());
      const { data, info } = await sharp(buf)
        .resize(SIZE, SIZE, { fit: 'cover', position: 'top' })
        .ensureAlpha()
        .raw()
        .toBuffer({ resolveWithObject: true });
      await writeFile(out, toLvgl(data, info.width, info.height));
      ok++;
    } catch { skip++; }
  }
  console.log(`  ${slug}:${teamId}  ${ok} built, ${had} already present, ${skip} unavailable`);
}

const args = process.argv.slice(2);
if (!args.length) {
  console.error('usage: npm run headshots -- <league:teamId> [more...]');
  console.error('e.g.:  npm run headshots -- nhl:21 mlb:14');
  process.exit(1);
}
console.log('Building headshots. These are personal likenesses — never commit them.');
for (const a of args) {
  const [slug, id] = a.split(':');
  if (!slug || !id) { console.error(`  bad argument ${a}`); continue; }
  await buildTeam(slug, id);
}
