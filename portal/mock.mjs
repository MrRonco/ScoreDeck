// Serve portal/index.html with a stub device API, so the page can be developed
// and reviewed in a browser without flashing anything.
//
//   node portal/mock.mjs   ->  http://127.0.0.1:8080
import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const HERE = dirname(fileURLToPath(import.meta.url));

const CONFIG = {
  v: '0.1.0-alpha.1', host: 'scoredeck.local', ip: '192.168.20.161',
  ssid: 'Rogers-5G', proxy: 'http://192.168.20.11:8787',
  hasToken: true, hasPass: false, rgn: 'ca',
  tz: 'EST5EDT,M3.2.0,M11.1.0', favs: 'nhl:21,nhl:10,eng.1:359',
  dens: 3, alen: 1, focus: 1, qen: 1, qfr: 1380, qto: 420,
  slot: 3538944, now: '9:14 PM',
};

const STATE = {
  net: 4, games: 9, live: 4, next: 8, heap: 121000,
  b: [
    { l: 'nhl', a: 'MTL', h: 'TOR', as: 2, hs: 3, g: 1, st: '3rd 04:21', b: 'SN Ontario', f: 1, ac: 'AF1E2D' },
    { l: 'nfl', a: 'BUF', h: 'KC',  as: 14, hs: 21, g: 1, st: 'Q2 11:03', b: 'CBS', ac: '00338D' },
    { l: 'nhl', a: 'EDM', h: 'CGY', as: 1, hs: 0, g: 1, st: '1st 18:44', b: 'SN', ac: 'FF4C00' },
    { l: 'mlb', a: 'CIN', h: 'WSH', as: 1, hs: 3, g: 1, st: 'Bot 7', b: 'MLB.TV', ac: 'C6011F' },
    { l: 'nhl', a: 'BOS', h: 'NYR', as: 0, hs: 0, g: 0, st: '7:00 PM', b: 'ESPN', ac: 'FFB81C' },
    { l: 'nfl', a: 'DAL', h: 'PHI', as: 17, hs: 27, g: 2, st: 'Final', b: '', ac: '041E42' },
  ],
};

const DIAG = {
  heap: 121000, largest: 42000, gate: 1, rssi: -54, up: 271860,
  reset: 'SW_RESTART', sleep: 0, pollAge: 8, pollCode: 200, pollMs: 96, next: 4,
  declGate: 3, declFlight: 0, declNoProxy: 0, stale: 0,
  seq: 4471, proxySeq: 4471, logoHit: 412, logoMiss: 18, games: 9, live: 4,
};

const json = (res, body) => {
  res.writeHead(200, { 'content-type': 'application/json', 'cache-control': 'no-store' });
  res.end(JSON.stringify(body));
};

createServer(async (req, res) => {
  const url = new URL(req.url, 'http://x');
  if (url.pathname === '/') {
    const html = await readFile(join(HERE, 'index.html'));
    res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
    return res.end(html);
  }
  if (url.pathname === '/api/config') return json(res, CONFIG);
  if (url.pathname === '/api/state')  return json(res, STATE);
  if (url.pathname === '/api/diag')   return json(res, DIAG);
  if (url.pathname === '/api/probe')  return json(res, { code: 200, ms: 41 });
  if (url.pathname === '/api/relay') {
    // A tiny stand-in catalog so the picker can be exercised offline.
    return json(res, { v: 1, t: [
      ['nhl', 'NHL', 'hockey', [['21', 'TOR', 'Toronto Maple Leafs', 0x00205B],
                                ['10', 'MTL', 'Montreal Canadiens', 0xAF1E2D],
                                ['6',  'BOS', 'Boston Bruins', 0xFFB81C]]],
      ['eng.1', 'EPL', 'soccer', [['359', 'ARS', 'Arsenal', 0xEF0107],
                                  ['364', 'LIV', 'Liverpool', 0xC8102E],
                                  ['393', 'NOT', 'Nottingham Forest', 0xDD0000]]],
    ] });
  }
  if (req.method === 'POST') return json(res, { ok: true });
  res.writeHead(404); res.end('not found');
}).listen(8080, () => console.log('portal mock on http://127.0.0.1:8080'));
