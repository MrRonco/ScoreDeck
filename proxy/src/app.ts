import { Hono } from 'hono';
import { LEAGUES, league } from './registry.ts';
import {
  fetchScoreboard, normalizeBoard, sortAndCap, fetchStandings, normalizeStandings,
  fetchTeams, normalizeTeams, fetchSummary, normalizeGame, fetchNews, normalizeNews,
  normalizeLineup, fetchAthlete, normalizePlayer, fetchStory, normalizeStory,
  normalizeTennis, normalizeGolf, normalizeF1,
} from './espn.ts';
import { diffBoard, nextPollSeconds } from './diff.ts';
import { MemoryStore, loadDiff, saveDiff, type Store } from './store.ts';
import { GS, SM, type Game, type ScoreEvent, type StateResponse, type NewsItem, type Story } from './types.ts';

export interface Env {
  store: Store;
  token?: string;
  /** When token is unset the proxy fails closed (503) unless this is true. */
  allowAnonymous?: boolean;
  fetchImpl?: typeof fetch;
  /** Reads a built asset by relative path, or undefined when absent. */
  assets?: (path: string) => Promise<ArrayBuffer | undefined>;
}

/** Favourites arrive as `nhl:21,eng.1:359`. Cap hard — this is a URL. */
function parseFavs(f: string | undefined): Map<string, Set<string>> {
  const out = new Map<string, Set<string>>();
  if (!f) return out;
  for (const part of f.split(',').slice(0, 20)) {
    const [lg, id] = part.split(':');
    if (!lg || !id || !league(lg)) continue;
    if (!/^\d{1,10}$/.test(id)) continue; // straight off a query string
    (out.get(lg) ?? out.set(lg, new Set()).get(lg)!).add(id);
  }
  return out;
}

function parseLeagues(lg: string | undefined, favs: Map<string, Set<string>>): string[] {
  const asked = (lg ?? '').split(',').filter((s) => league(s));
  const set = new Set([...asked, ...favs.keys()]);
  if (!set.size) return ['nhl', 'nfl', 'nba', 'mlb'];
  return [...set].slice(0, 12);
}

/** "Today" is the device's local day, not US Eastern. See PLAN.md §3. */
function localDate(tz: string, now = new Date()): string {
  try {
    const p = new Intl.DateTimeFormat('en-CA', {
      timeZone: tz, year: 'numeric', month: '2-digit', day: '2-digit',
    }).format(now);
    return p.replaceAll('-', '');
  } catch {
    return new Intl.DateTimeFormat('en-CA', {
      timeZone: 'UTC', year: 'numeric', month: '2-digit', day: '2-digit',
    }).format(now).replaceAll('-', '');
  }
}

function safeTz(tz: string | undefined): string {
  if (!tz) return 'UTC';
  try {
    new Intl.DateTimeFormat('en-US', { timeZone: tz });
    return tz;
  } catch {
    return 'UTC';
  }
}

/** Length-checked constant-time string compare — no early-out on the token. */
function safeEqual(a: string, b: string): boolean {
  if (a.length !== b.length) return false;
  let diff = 0;
  for (let i = 0; i < a.length; i++) diff |= a.charCodeAt(i) ^ b.charCodeAt(i);
  return diff === 0;
}

/** Wrap an upstream fetch with a hard timeout and a response-body byte cap.
 *  ESPN's largest known payload is the 1.2 MB golf scoreboard, so 3 MB is
 *  generous headroom; anything past it is treated as hostile/broken and the
 *  read is aborted rather than buffered without bound. */
function guardedFetch(base: typeof fetch, maxBytes = 3_000_000, timeoutMs = 8000): typeof fetch {
  return (async (input: any, init?: any) => {
    const res = await base(input, { ...(init ?? {}), signal: AbortSignal.timeout(timeoutMs) });
    if (!res.ok || !res.body) return res;
    const reader = res.body.getReader();
    const chunks: Uint8Array[] = [];
    let total = 0;
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      total += value.byteLength;
      if (total > maxBytes) { await reader.cancel(); throw new Error('upstream body exceeded cap'); }
      chunks.push(value);
    }
    const buf = new Uint8Array(total);
    let o = 0;
    for (const ch of chunks) { buf.set(ch, o); o += ch.byteLength; }
    return new Response(buf, { status: res.status, statusText: res.statusText, headers: res.headers });
  }) as typeof fetch;
}

export function createApp(env: Env) {
  const app = new Hono();
  const doFetch = guardedFetch(env.fetchImpl ?? fetch);
  // Single-flight: concurrent cold builds of the same expensive key share one
  // computation instead of each doing ~22 upstream fetches (M4).
  const inflight = new Map<string, Promise<unknown>>();
  const once = <T>(k: string, make: () => Promise<T>): Promise<T> => {
    const cur = inflight.get(k);
    if (cur) return cur as Promise<T>;
    const p = make().finally(() => inflight.delete(k));
    inflight.set(k, p);
    return p;
  };

  /**
   * The browser portal is served from the DEVICE and fetches the catalog from
   * the PROXY, so those requests are cross-origin and die at preflight without
   * this. Scoped deliberately: only the read-only reference routes, never
   * /v1/state.
   *
   * `*` is the right origin here because these fetches carry an explicit
   * Authorization header and `credentials: 'omit'` — there are no ambient
   * credentials for a hostile page to ride on, and the bearer token still
   * gates every response.
   */
  const CORS_PATHS = /^\/v1\/(catalog|teams\/[a-z0-9.]{2,8}|health)$/;

  app.use('*', async (c, next) => {
    const corsOk = CORS_PATHS.test(new URL(c.req.url).pathname);
    if (corsOk && c.req.method === 'OPTIONS') {
      return new Response(null, {
        status: 204,
        headers: {
          'access-control-allow-origin': '*',
          'access-control-allow-headers': 'authorization',
          'access-control-allow-methods': 'GET, OPTIONS',
          'access-control-max-age': '86400',
        },
      });
    }
    if (env.token) {
      const auth = c.req.header('authorization') ?? '';
      if (!safeEqual(auth, `Bearer ${env.token}`)) return c.json({ error: 'unauthorized' }, 401);
    } else if (!env.allowAnonymous) {
      // Fail closed: an unconfigured token is a deployment mistake, not an
      // invitation to run open. Explicit SD_ALLOW_ANONYMOUS=1 opts out.
      return c.json({ error: 'proxy has no SD_TOKEN configured' }, 503);
    }
    await next();
    if (corsOk) c.header('access-control-allow-origin', '*');
  });

  app.get('/v1/health', async (c) =>
    c.json({ ok: true, leagues: LEAGUES.length, now: Math.floor(Date.now() / 1000) }));

  /**
   * Leagues by default; the whole team catalog with `?teams=1`.
   *
   * The full set is ~1,900 teams. As objects that is ~120 KB, which does not
   * fit on a device holding ~120 KB of free heap and needing 16 KB of it for
   * TLS — so the DEVICE never asks for this. Only the browser does, and it
   * caches the answer. Packed as arrays rather than objects because the keys
   * would otherwise be about a third of the payload.
   */
  app.get('/v1/catalog', async (c) => {
    const leagues = LEAGUES.map(({ slug, label, family }) => ({ slug, label, family }));
    if (c.req.query('teams') !== '1') return c.json({ leagues });

    const key = 'catalog:teams:v1';
    const hit = await env.store.get(key);
    if (hit) return c.json(hit);

    const body = await once(key, async () => {
      const again = await env.store.get(key);
      if (again) return again;
      // Tennis, golf and racing have no team endpoint — they are fields of
      // individuals, and asking would 404.
      const teamLeagues = LEAGUES.filter((l) => l.model !== SM.SET &&
                                                l.model !== SM.LEADERBOARD &&
                                                l.model !== SM.GRID);
      const out: any[] = [];
      for (const lg of teamLeagues) {
        try {
          const tk = `teams:${lg.slug}`;
          const cached = await env.store.get(tk);
          const teams = cached ?? normalizeTeams(await fetchTeams(lg, doFetch), lg);
          if (!cached) await env.store.put(tk, teams, 24 * 3600);
          out.push([lg.slug, lg.label, lg.family,
                    (teams as any[]).map((t) => [t.id, t.a, t.n, t.c ?? 0])]);
        } catch {
          // One league failing upstream must not empty the whole catalog.
          out.push([lg.slug, lg.label, lg.family, []]);
        }
      }
      const b = { v: 1, leagues, t: out };
      await env.store.put(key, b, 24 * 3600);
      return b;
    });
    return c.json(body);
  });

  app.get('/v1/state', async (c) => {
    const q = c.req.query();
    const favs = parseFavs(q.f);
    const slugs = parseLeagues(q.lg, favs);
    const region = /^[a-z]{2}$/.test(q.rgn ?? '') ? q.rgn! : 'us';
    const tz = safeTz(q.tz);
    const sinceSeq = Number.parseInt(q.seq ?? '0', 10) || 0;
    const quiet = q.quiet === '1';
    const date = localDate(tz);

    const favTeamKeys = new Set<string>();
    const favAbbrs = new Set<string>();
    for (const [slug, ids] of favs) for (const id of ids) favTeamKeys.add(`${slug}:${id}`);

    let all: Game[] = [];
    const events: ScoreEvent[] = [];
    const counts: { l: string; n: number }[] = [];
    let stale = false;
    let maxSeq = 0;

    for (const slug of slugs) {
      const lg = league(slug)!;
      const cacheKey = `board:${slug}:${date}:${region}`;
      let games = await env.store.get<Game[]>(cacheKey);

      if (!games) {
        try {
          const raw = await fetchScoreboard(lg, date, doFetch);
          const opts = { region, tz, favTeamKeys, favAbbrs };
          // Three sports break the two-sided assumption; each gets its own
          // normalizer rather than bending normalizeBoard out of shape.
          games = lg.model === SM.SET         ? normalizeTennis(raw, lg, opts)
                : lg.model === SM.LEADERBOARD ? normalizeGolf(raw, lg, opts)
                : lg.model === SM.GRID        ? normalizeF1(raw, lg, opts)
                :                               normalizeBoard(raw, lg, opts);
          await env.store.put(cacheKey, games, 20);
        } catch {
          // Upstream failed. Say so rather than silently showing an empty board.
          stale = true;
          games = (await env.store.get<Game[]>(`last:${slug}`)) ?? [];
        }
      }
      if (games.length) await env.store.put(`last:${slug}`, games, 6 * 3600);

      const prior = await loadDiff(env.store, slug);
      const d = diffBoard(prior.snaps, games, prior.seq);
      if (d.events.length || prior.snaps.size !== d.next.size) {
        await saveDiff(env.store, slug, d.seq, d.next);
      }
      maxSeq = Math.max(maxSeq, d.seq);
      // Only alert on games involving a followed team.
      const favIds = favs.get(slug);
      for (const e of d.events) {
        const g = games.find((x) => x.i === e.i);
        if (!favIds || !g) continue;
        if (favIds.has(g.home.id) || favIds.has(g.away.id)) events.push(e);
      }

      counts.push({ l: slug, n: games.filter((g) => g.g === GS.LIVE).length });
      all = all.concat(games);
    }

    const games = sortAndCap(all);
    const body: StateResponse = {
      v: 1,
      now: Math.floor(Date.now() / 1000),
      games,
      ev: events.filter((e) => e.q > sinceSeq).sort((a, b) => a.q - b.q).slice(0, 8),
      seq: maxSeq,
      lg: counts,
      next: nextPollSeconds(games, quiet),
      ...(stale ? { stale: true } : {}),
    };
    return c.json(body);
  });

  // Team directory. This is how you discover the ids that go in `favs`:
  //   GET /v1/teams/nhl  ->  [{ id: "21", a: "TOR", n: "Toronto Maple Leafs" }, ...]
  // Logo blobs, built locally by tools/build-logos.mjs. Never shipped with the
  // project — see docs/OPEN_SOURCE.md §1. A miss is a 404 and the device falls
  // back to its colour badge, so an unbuilt install still looks deliberate.
  app.get('/v1/logo/:league/:file', async (c) => {
    const lg = league(c.req.param('league'));
    if (!lg) return c.notFound();
    const file = c.req.param('file');
    if (!/^[A-Za-z0-9]{1,5}@(48|96)\.bin$/.test(file)) return c.json({ error: 'bad name' }, 400);
    const bytes = await env.assets?.(`logos/${lg.slug}/${file}`);
    if (!bytes) return c.notFound();
    return c.body(bytes, 200, {
      'content-type': 'application/octet-stream',
      'cache-control': 'public, max-age=31536000, immutable',
    });
  });

  app.get('/v1/news', async (c) => {
    const favs = parseFavs(c.req.query('f'));
    const slugs = parseLeagues(c.req.query('lg'), favs).slice(0, 4);

    // Abbreviations come from the board cache, which already resolved ids.
    const favAbbrs = new Set<string>();
    const colors = new Map<string, number>();
    for (const slug of slugs) {
      const ids = favs.get(slug);
      for (const key of ['us', 'ca', 'gb'] as const) {
        const board = await env.store.get<Game[]>(`last:${slug}`);
        if (!board) continue;
        for (const g of board) {
          for (const side of [g.away, g.home]) {
            colors.set(side.a, side.c);
            if (ids?.has(side.id)) favAbbrs.add(side.a);
          }
        }
        break;
      }
    }

    const items: NewsItem[] = [];
    for (const slug of slugs) {
      const lg = league(slug)!;
      const key = `news:${slug}`;
      let got = await env.store.get<NewsItem[]>(key);
      if (!got) {
        try {
          got = normalizeNews(await fetchNews(lg, doFetch), lg, favAbbrs, (a) => colors.get(a));
          await env.store.put(key, got, 900);
        } catch {
          got = [];
        }
      }
      items.push(...got);
    }

    // De-duplicate: the same story is often syndicated across leagues.
    const seen = new Set<string>();
    const unique = items.filter((i) => !seen.has(i.h) && seen.add(i.h));
    unique.sort((x, y) => (Number(!!y.a) - Number(!!x.a)) || (y.t - x.t));
    return c.json({ v: 1, items: unique.slice(0, 10) });
  });

  app.get('/v1/story/:id', async (c) => {
    const id = c.req.param('id');
    if (!/^\d{1,12}$/.test(id)) return c.json({ error: 'bad id' }, 400);
    const key = `story:${id}`;
    let got = await env.store.get<Story>(key);
    if (!got) {
      try {
        const s = normalizeStory(await fetchStory(id, doFetch));
        if (!s) return c.json({ error: 'no story body' }, 404);
        got = s;
        await env.store.put(key, got, 3600);   // stories do not change
      } catch {
        return c.json({ error: 'upstream failed' }, 502);
      }
    }
    return c.json(got);
  });

  app.get('/v1/game/:league/:id', async (c) => {
    const lg = league(c.req.param('league'));
    if (!lg) return c.json({ error: 'unknown league' }, 404);
    const id = c.req.param('id');
    if (!/^\d{1,12}$/.test(id)) return c.json({ error: 'bad id' }, 400);
    const region = /^[a-z]{2}$/.test(c.req.query('rgn') ?? '') ? c.req.query('rgn')! : 'us';

    const key = `game:${lg.slug}:${id}:${region}`;
    const hit = await env.store.get(key);
    if (hit) return c.json(hit);
    try {
      const detail = normalizeGame(await fetchSummary(lg, id, doFetch), lg, region);
      if (!detail) return c.json({ error: 'no such game' }, 404);
      // Short TTL: a live game's linescore and plays move constantly.
      await env.store.put(key, detail, detail.live ? 12 : 300);
      return c.json(detail);
    } catch {
      return c.json({ error: 'upstream' }, 502);
    }
  });

  app.get('/v1/lineup/:league/:id', async (c) => {
    const lg = league(c.req.param('league'));
    if (!lg) return c.json({ error: 'unknown league' }, 404);
    const id = c.req.param('id');
    if (!/^\d{1,12}$/.test(id)) return c.json({ error: 'bad id' }, 400);
    const key = `lineup:${lg.slug}:${id}`;
    const hit = await env.store.get(key);
    if (hit) return c.json(hit);
    try {
      const l = normalizeLineup(await fetchSummary(lg, id, doFetch), lg);
      if (!l) return c.json({ error: 'no lineup' }, 404);
      await env.store.put(key, l, 60);
      return c.json(l);
    } catch {
      return c.json({ error: 'upstream' }, 502);
    }
  });

  app.get('/v1/player/:league/:id', async (c) => {
    const lg = league(c.req.param('league'));
    if (!lg) return c.json({ error: 'unknown league' }, 404);
    const id = c.req.param('id');
    if (!/^\d{1,12}$/.test(id)) return c.json({ error: 'bad id' }, 400);
    const key = `player:${lg.slug}:${id}`;

    // Whether a headshot exists describes OUR assets, not ESPN's data, so it is
    // resolved on every request and deliberately NOT cached with the payload.
    // Cached alongside it, running the headshot build had no visible effect for
    // six hours: every player anyone had already opened kept reporting img
    // false, and the device kept drawing the jersey badge.
    const hasImg = async () => !!(await env.assets?.(`players/${lg.slug}/${id}.bin`));

    const hit = await env.store.get(key);
    if (hit) return c.json({ ...(hit as object), img: await hasImg() });
    try {
      const p = normalizePlayer(await fetchAthlete(lg, id, doFetch), lg);
      if (!p) return c.json({ error: 'no such player' }, 404);
      // Store WITHOUT img so the cached copy can never go stale against the
      // asset directory.
      await env.store.put(key, p, 6 * 3600);
      p.img = await hasImg();
      return c.json(p);
    } catch {
      return c.json({ error: 'upstream' }, 502);
    }
  });

  app.get('/v1/head/:league/:file', async (c) => {
    const lg = league(c.req.param('league'));
    if (!lg) return c.notFound();
    const file = c.req.param('file');
    if (!/^\d{1,12}\.bin$/.test(file)) return c.json({ error: 'bad name' }, 400);
    const bytes = await env.assets?.(`players/${lg.slug}/${file}`);
    if (!bytes) return c.notFound();
    return c.body(bytes, 200, {
      'content-type': 'application/octet-stream',
      'cache-control': 'public, max-age=31536000, immutable',
    });
  });

  app.get('/v1/teams/:league', async (c) => {
    const lg = league(c.req.param('league'));
    if (!lg) return c.json({ error: 'unknown league' }, 404);
    const key = `teams:${lg.slug}`;
    const hit = await env.store.get(key);
    if (hit) return c.json(hit);
    try {
      const teams = normalizeTeams(await fetchTeams(lg, doFetch), lg);
      await env.store.put(key, teams, 24 * 3600);
      return c.json(teams);
    } catch {
      return c.json({ error: 'upstream' }, 502);
    }
  });

  app.get('/v1/standings/:league', async (c) => {
    const lg = league(c.req.param('league'));
    if (!lg) return c.json({ error: 'unknown league' }, 404);
    const grp = Math.min(Math.max(0, Number.parseInt(c.req.query('grp') ?? '0', 10) || 0), 16);
    const key = `stand:${lg.slug}:${grp}`;
    const hit = await env.store.get(key);
    if (hit) return c.json(hit);
    try {
      const table = normalizeStandings(await fetchStandings(lg, doFetch), lg, grp);
      await env.store.put(key, table, 600);
      return c.json(table);
    } catch {
      return c.json({ error: 'upstream' }, 502);
    }
  });

  app.notFound((c) => c.json({ error: 'not found' }, 404));
  return app;
}

export const defaultEnv = (): Env => ({ store: new MemoryStore() });
