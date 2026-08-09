import { Hono } from 'hono';
import { LEAGUES, league } from './registry.ts';
import {
  fetchScoreboard, normalizeBoard, sortAndCap, fetchStandings, normalizeStandings,
  fetchTeams, normalizeTeams,
} from './espn.ts';
import { diffBoard, nextPollSeconds } from './diff.ts';
import { MemoryStore, loadDiff, saveDiff, type Store } from './store.ts';
import { GS, type Game, type ScoreEvent, type StateResponse } from './types.ts';

export interface Env {
  store: Store;
  token?: string;
  fetchImpl?: typeof fetch;
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

export function createApp(env: Env) {
  const app = new Hono();
  const doFetch = env.fetchImpl ?? fetch;

  app.use('*', async (c, next) => {
    if (env.token) {
      const auth = c.req.header('authorization');
      if (auth !== `Bearer ${env.token}`) return c.json({ error: 'unauthorized' }, 401);
    }
    await next();
  });

  app.get('/v1/health', async (c) =>
    c.json({ ok: true, leagues: LEAGUES.length, now: Math.floor(Date.now() / 1000) }));

  app.get('/v1/catalog', (c) =>
    c.json({ leagues: LEAGUES.map(({ slug, label, family }) => ({ slug, label, family })) }));

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
          games = normalizeBoard(raw, lg, { region, tz, favTeamKeys, favAbbrs });
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
    const grp = Number.parseInt(c.req.query('grp') ?? '0', 10) || 0;
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
