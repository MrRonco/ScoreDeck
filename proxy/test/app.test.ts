import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createApp } from '../src/app.ts';
import { MemoryStore } from '../src/store.ts';
import type { StateResponse } from '../src/types.ts';

/** A stub ESPN whose score we can move between calls. */
function stubEspn(state: { homeScore: number }) {
  return async (url: string | URL | Request): Promise<Response> => {
    const u = String(url);
    if (!u.includes('/scoreboard')) return new Response('{}', { status: 404 });
    return Response.json({
      events: [{
        id: '900001',
        date: '2026-11-08T00:00Z',
        status: { period: 3, type: { state: 'in', shortDetail: '3rd - 4:21' } },
        competitions: [{
          geoBroadcasts: [{ market: { type: 'National' }, media: { shortName: 'SN' }, region: 'us' }],
          competitors: [
            { homeAway: 'home', score: String(state.homeScore),
              team: { id: '21', abbreviation: 'TOR', shortDisplayName: 'Maple Leafs', color: '00205B' } },
            { homeAway: 'away', score: '2',
              team: { id: '10', abbreviation: 'MTL', shortDisplayName: 'Canadiens', color: 'AF1E2D' } },
          ],
        }],
      }],
    });
  };
}

const get = async (app: ReturnType<typeof createApp>, q: string, token?: string) => {
  const headers = token ? { authorization: `Bearer ${token}` } : undefined;
  const res = await app.fetch(new Request(`http://t/v1/state?${q}`, { headers }));
  return { res, body: (await res.json()) as StateResponse };
};

test('a goal between polls produces exactly one event, once', async () => {
  const espn = { homeScore: 3 };
  const app = createApp({ store: new MemoryStore(), allowAnonymous: true, fetchImpl: stubEspn(espn) });
  const q = 'lg=nhl&f=nhl:21&rgn=ca&tz=America/Toronto';

  // First observation establishes the baseline and must NOT alert.
  const first = await get(app, q);
  assert.equal(first.res.status, 200);
  assert.equal(first.body.games.length, 1);
  assert.equal(first.body.ev.length, 0, 'a device that was off must not replay');
  assert.equal(first.body.games[0]!.f, true);
  assert.equal(first.body.games[0]!.b, 'SN Ontario', 'region ca resolves from rights.json');

  // TOR scores. The board cache has a 20 s TTL, so use a fresh app sharing the
  // same store to force an upstream refetch rather than sleeping in a test.
  const store = new MemoryStore();
  const a1 = createApp({ store, allowAnonymous: true, fetchImpl: stubEspn(espn) });
  await get(a1, q);
  espn.homeScore = 4;
  const a2 = createApp({ store, allowAnonymous: true, fetchImpl: stubEspn(espn) });
  // Bust only the board cache; diff state persists in the same store.
  await store.put('board:nhl:' + new Intl.DateTimeFormat('en-CA', {
    timeZone: 'America/Toronto', year: 'numeric', month: '2-digit', day: '2-digit',
  }).format(new Date()).replaceAll('-', '') + ':ca', undefined, 0);

  const second = await get(a2, q);
  assert.equal(second.body.ev.length, 1, 'exactly one event for one goal');
  const ev = second.body.ev[0]!;
  assert.equal(ev.v, 'GOAL');
  assert.equal(ev.a, 'TOR');
  assert.deepEqual(ev.s, [2, 4]);
  assert.ok(ev.q > 0);

  // Replaying with the device's new seq must not re-deliver it.
  const third = await get(a2, `${q}&seq=${second.body.seq}`);
  assert.equal(third.body.ev.length, 0, 'seq filters what the device already saw');
});

test('the response fits the device budget and carries a poll cadence', async () => {
  const app = createApp({ store: new MemoryStore(), allowAnonymous: true, fetchImpl: stubEspn({ homeScore: 3 }) });
  const { body } = await get(app, 'lg=nhl&f=nhl:21&rgn=ca&tz=America/Toronto');
  assert.equal(body.v, 1);
  assert.equal(body.next, 12, 'a followed team is live, so poll tightly');
  assert.ok(body.now > 1_700_000_000);
  assert.deepEqual(body.lg, [{ l: 'nhl', n: 1 }]);
  assert.ok(JSON.stringify(body).length < 3000, 'one game must be far inside the 3 KB budget');
});

test('a bearer token is enforced when configured', async () => {
  const app = createApp({ store: new MemoryStore(), token: 'sekrit', fetchImpl: stubEspn({ homeScore: 3 }) });
  const anon = await app.fetch(new Request('http://t/v1/health'));
  assert.equal(anon.status, 401);
  const authed = await app.fetch(
    new Request('http://t/v1/health', { headers: { authorization: 'Bearer sekrit' } }),
  );
  assert.equal(authed.status, 200);
});

test('an upstream failure reports stale rather than an empty board', async () => {
  const store = new MemoryStore();
  const ok = createApp({ store, allowAnonymous: true, fetchImpl: stubEspn({ homeScore: 3 }) });
  await get(ok, 'lg=nhl&tz=UTC');           // seed last-known-good

  const dead = createApp({
    allowAnonymous: true,
    store,
    fetchImpl: async () => new Response('nope', { status: 503 }),
  });
  // Expire the fresh board so the failing path is taken.
  const key = 'board:nhl:' + new Intl.DateTimeFormat('en-CA', {
    timeZone: 'UTC', year: 'numeric', month: '2-digit', day: '2-digit',
  }).format(new Date()).replaceAll('-', '') + ':us';
  await store.put(key, undefined, 0);

  const { body } = await get(dead, 'lg=nhl&tz=UTC');
  assert.equal(body.stale, true, 'the device must be told, not silently shown nothing');
  assert.equal(body.games.length, 1, 'last known good is served rather than a blank screen');
});

test('unknown leagues and bad favourites are rejected, not trusted', async () => {
  const app = createApp({ store: new MemoryStore(), allowAnonymous: true, fetchImpl: stubEspn({ homeScore: 3 }) });
  const { body } = await get(app, 'lg=nhl&f=nhl:21,quidditch:1,nhl:DROP%20TABLE&tz=UTC');
  assert.equal(body.games.length, 1);
  const res = await app.fetch(new Request('http://t/v1/standings/quidditch'));
  assert.equal(res.status, 404);
});

/**
 * The portal is served from the device and fetches the catalog from the proxy,
 * so these are cross-origin. Without CORS they die at preflight — and the
 * failure looks like "the proxy is down" rather than "the browser refused".
 */
test('CORS is allowed on the reference routes and withheld from /v1/state', async () => {
  const app = createApp({ store: new MemoryStore(), allowAnonymous: true, fetchImpl: stubEspn({ homeScore: 3 }) });

  const pre = await app.fetch(new Request('http://x/v1/catalog', { method: 'OPTIONS' }));
  assert.equal(pre.status, 204);
  assert.equal(pre.headers.get('access-control-allow-origin'), '*');
  assert.equal(pre.headers.get('access-control-allow-methods'), 'GET, OPTIONS');

  const cat = await app.fetch(new Request('http://x/v1/catalog'));
  assert.equal(cat.headers.get('access-control-allow-origin'), '*');

  // Scores are not a reference route and must not be readable cross-origin.
  const state = await app.fetch(new Request('http://x/v1/state?lg=nhl'));
  assert.equal(state.headers.get('access-control-allow-origin'), null);
});

test('preflight does not bypass the bearer check for the actual request', async () => {
  const app = createApp({ store: new MemoryStore(), token: 'secret',
                          fetchImpl: stubEspn({ homeScore: 3 }) });
  // The preflight itself carries no credentials by design...
  assert.equal((await app.fetch(new Request('http://x/v1/catalog', { method: 'OPTIONS' }))).status, 204);
  // ...but the GET behind it still has to authenticate.
  assert.equal((await app.fetch(new Request('http://x/v1/catalog'))).status, 401);
});

test('?teams=1 packs the catalog as arrays and skips the individual sports', async () => {
  const fetchImpl = async (url: string | URL | Request): Promise<Response> => {
    const u = String(url);
    if (u.includes('/teams')) {
      return Response.json({ sports: [{ leagues: [{ teams: [
        { team: { id: '21', abbreviation: 'TOR', displayName: 'Toronto Maple Leafs', color: '00205B' } },
      ] }] }] });
    }
    return new Response('{}', { status: 404 });
  };
  const app = createApp({ store: new MemoryStore(), allowAnonymous: true, fetchImpl });
  const body: any = await (await app.fetch(new Request('http://x/v1/catalog?teams=1'))).json();

  assert.equal(body.v, 1);
  const slugs = body.t.map((row: any[]) => row[0]);
  // Tennis, golf and racing are fields of individuals — there is no team
  // endpoint for them and asking would 404.
  for (const s of ['atp', 'wta', 'pga', 'lpga', 'f1']) assert.ok(!slugs.includes(s), s);
  assert.ok(slugs.includes('nhl'));

  const nhl = body.t.find((row: any[]) => row[0] === 'nhl');
  assert.deepEqual(nhl[3][0].slice(0, 3), ['21', 'TOR', 'Toronto Maple Leafs']);
});

/**
 * Whether a headshot exists is a fact about OUR assets, not about ESPN, so it
 * must not be cached with the upstream payload. It was, and the effect was
 * that running the headshot build did nothing for six hours: every player
 * anyone had already opened kept reporting img false and the panel kept
 * drawing the jersey badge.
 */
test('img reflects the asset directory even on a cache hit', async () => {
  const present = new Set<string>();
  const app = createApp({
    store: new MemoryStore(),
    allowAnonymous: true,
    assets: async (p: string) => (present.has(p) ? new Uint8Array([1]) : null),
    fetchImpl: async (url: string | URL | Request) => {
      if (!String(url).includes('/athletes/')) return new Response('{}', { status: 404 });
      return Response.json({ athlete: { id: '99', displayName: 'A Player',
                                        position: { displayName: 'Catcher' } } });
    },
  } as any);

  const one: any = await (await app.fetch(new Request('http://x/v1/player/mlb/99'))).json();
  assert.equal(one.img, false, 'no blob yet');

  // The build runs. Nothing about ESPN changed, so the payload is still cached.
  present.add('players/mlb/99.bin');

  const two: any = await (await app.fetch(new Request('http://x/v1/player/mlb/99'))).json();
  assert.equal(two.img, true, 'a cache hit must still re-check the assets');
  assert.equal(two.n, 'A Player', 'and must still serve the cached payload');
});

test('a proxy with no token and no opt-out fails closed (503)', async () => {
  const app = createApp({ store: new MemoryStore(), fetchImpl: stubEspn({ homeScore: 3 }) });
  const res = await app.fetch(new Request('http://t/v1/health'));
  assert.equal(res.status, 503);
  const ok = createApp({ store: new MemoryStore(), allowAnonymous: true, fetchImpl: stubEspn({ homeScore: 3 }) });
  assert.equal((await ok.fetch(new Request('http://t/v1/health'))).status, 200);
});

test('a wrong token is rejected in constant time (still 401)', async () => {
  const app = createApp({ store: new MemoryStore(), token: 'sekrit', fetchImpl: stubEspn({ homeScore: 3 }) });
  const res = await app.fetch(new Request('http://t/v1/health', { headers: { authorization: 'Bearer nope' } }));
  assert.equal(res.status, 401);
});
