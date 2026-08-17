// SPDX-License-Identifier: AGPL-3.0-or-later
// Cloudflare Workers entry point.
//
// The only Cloudflare-specific surface in the project. Everything else runs
// unmodified on Node, a Pi, or Fly.io — see server.ts.
import { createApp } from './app.ts';
import type { Store } from './store.ts';
import { LEAGUES } from './registry.ts';

interface WorkerEnv {
  /** Set with: wrangler secret put SD_TOKEN */
  SD_TOKEN?: string;
  /** "1" to run without a token (fails closed otherwise). */
  SD_ALLOW_ANONYMOUS?: string;
}

/**
 * Cache API rather than KV, deliberately.
 *
 * KV on the free plan allows 1,000 writes/day; a 60-second cron across a
 * handful of leagues blows through that before lunch. The Cache API has no
 * such write cap. The trade-off is that it is per-colo rather than global,
 * which is exactly right here — a board is regional anyway.
 */
class CacheStore implements Store {
  #base = 'https://scoredeck.internal/';

  async get<T>(key: string): Promise<T | undefined> {
    const res = await caches.default.match(this.#base + encodeURIComponent(key));
    if (!res) return undefined;
    try {
      return (await res.json()) as T;
    } catch {
      return undefined;
    }
  }

  async put<T>(key: string, value: T, ttlSeconds: number): Promise<void> {
    // A zero TTL is how callers invalidate; Cache API needs an explicit delete.
    if (ttlSeconds <= 0 || value === undefined) {
      await caches.default.delete(this.#base + encodeURIComponent(key));
      return;
    }
    await caches.default.put(
      this.#base + encodeURIComponent(key),
      new Response(JSON.stringify(value), {
        headers: {
          'content-type': 'application/json',
          'cache-control': `max-age=${Math.floor(ttlSeconds)}`,
        },
      }),
    );
  }
}

export default {
  async fetch(request: Request, env: WorkerEnv): Promise<Response> {
    const app = createApp({
      store: new CacheStore(),
      token: env.SD_TOKEN,
      allowAnonymous: env.SD_ALLOW_ANONYMOUS === '1',
    });
    return app.fetch(request);
  },

  /**
   * Cron warms the board cache so a device request is a cache read plus a
   * filter, well inside the free plan's 10 ms per-request CPU budget. The
   * heavy normalising happens here instead.
   */
  async scheduled(_event: ScheduledEvent, env: WorkerEnv, ctx: ExecutionContext): Promise<void> {
    const app = createApp({
      store: new CacheStore(),
      token: env.SD_TOKEN,
      allowAnonymous: env.SD_ALLOW_ANONYMOUS === '1',
    });
    const auth = env.SD_TOKEN ? { authorization: `Bearer ${env.SD_TOKEN}` } : undefined;

    // Warm only the in-season leagues a board is likely to ask for. Anything
    // nobody follows costs nothing because it is never requested.
    const warm = LEAGUES.filter((l) =>
      ['nhl', 'nfl', 'nba', 'mlb', 'eng.1'].includes(l.slug),
    );

    ctx.waitUntil(
      Promise.allSettled(
        warm.map((l) =>
          app.fetch(
            new Request(
              `https://scoredeck.internal/v1/state?lg=${encodeURIComponent(l.slug)}&tz=UTC`,
              { headers: auth },
            ),
          ),
        ),
      ),
    );
  },
};
