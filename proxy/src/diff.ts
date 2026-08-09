import { GS, type Game, type ScoreEvent } from './types.ts';
import { league, scoreVerb } from './registry.ts';

/** Last observed score for one game. */
export interface Snapshot {
  away: number;
  home: number;
  state: number;
}

/**
 * Score-diffing is why the proxy exists rather than the device doing it:
 * "TOR scored" is a *difference* between two observations, and the device
 * loses its memory on every power cycle.
 */
export function diffBoard(
  prev: Map<string, Snapshot>,
  games: Game[],
  startSeq: number,
): { events: ScoreEvent[]; seq: number; next: Map<string, Snapshot> } {
  const events: ScoreEvent[] = [];
  const next = new Map<string, Snapshot>();
  let seq = startSeq;

  for (const g of games) {
    const snap: Snapshot = { away: g.away.s, home: g.home.s, state: g.g };
    next.set(g.i, snap);

    const before = prev.get(g.i);
    // No prior observation → this is a first sighting, not a score. Never alert:
    // a device that was off overnight must not replay yesterday's goals.
    if (!before) continue;
    if (g.g !== GS.LIVE && before.state !== GS.LIVE) continue;

    const dAway = g.away.s - before.away;
    const dHome = g.home.s - before.home;
    if (dAway <= 0 && dHome <= 0) continue;

    const lg = league(g.l);
    const family = lg?.family ?? 'other';
    // A rare simultaneous update raises one event for the larger change.
    const homeScored = dHome >= dAway;
    const delta = homeScored ? dHome : dAway;
    const side = homeScored ? g.home : g.away;

    events.push({
      q: ++seq,
      i: g.i,
      l: g.l,
      v: scoreVerb(family, delta),
      a: side.a,
      c: side.c,
      s: [g.away.s, g.home.s],
      st: g.st,
    });
  }

  return { events, seq, next };
}

/**
 * Poll cadence, owned by the proxy so the device never has to reason about it.
 * See PLAN.md §7.
 */
export function nextPollSeconds(games: Game[], quiet: boolean): number {
  if (quiet) return 900;
  const favLive = games.some((g) => g.f && g.g === GS.LIVE);
  if (favLive) return 12;
  if (games.some((g) => g.g === GS.LIVE)) return 30;
  const now = Date.now() / 1000;
  if (games.some((g) => g.g === GS.PRE && g.t - now < 6 * 3600)) return 60;
  return 300;
}
