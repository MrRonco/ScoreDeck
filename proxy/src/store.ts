import type { Snapshot } from './diff.ts';

/**
 * The one Cloudflare-specific surface, behind an interface so the same code
 * runs on Node, a Pi, or a Worker. See OPEN_SOURCE.md §2.
 */
export interface Store {
  get<T>(key: string): Promise<T | undefined>;
  put<T>(key: string, value: T, ttlSeconds: number): Promise<void>;
}

export class MemoryStore implements Store {
  #m = new Map<string, { v: unknown; exp: number }>();
  readonly #cap: number;

  // Bounded so a caller minting unique keys (per-id story/standings routes)
  // cannot grow the map without limit. Insertion order is eviction order
  // (a Map iterates oldest-first), so deleting the first key is LRU-by-age.
  constructor(cap = 2000) { this.#cap = cap; }

  async get<T>(key: string): Promise<T | undefined> {
    const e = this.#m.get(key);
    if (!e) return undefined;
    if (e.exp < Date.now()) {
      this.#m.delete(key);
      return undefined;
    }
    // Touch: move to newest so genuinely-used keys survive eviction.
    this.#m.delete(key);
    this.#m.set(key, e);
    return e.v as T;
  }

  async put<T>(key: string, value: T, ttlSeconds: number): Promise<void> {
    this.#m.delete(key);
    this.#m.set(key, { v: value, exp: Date.now() + ttlSeconds * 1000 });
    if (this.#m.size > this.#cap) this.#sweep();
  }

  // Drop expired entries first; if still over cap, evict oldest until under.
  #sweep(): void {
    const now = Date.now();
    for (const [k, e] of this.#m) if (e.exp < now) this.#m.delete(k);
    while (this.#m.size > this.#cap) {
      const oldest = this.#m.keys().next().value;
      if (oldest === undefined) break;
      this.#m.delete(oldest);
    }
  }
}

/** Serialised diff state — Maps do not survive JSON. */
export interface DiffState {
  seq: number;
  snaps: [string, Snapshot][];
}

export async function loadDiff(store: Store, league: string): Promise<{ seq: number; snaps: Map<string, Snapshot> }> {
  const s = await store.get<DiffState>(`diff:${league}`);
  return { seq: s?.seq ?? 0, snaps: new Map(s?.snaps ?? []) };
}

export async function saveDiff(
  store: Store,
  league: string,
  seq: number,
  snaps: Map<string, Snapshot>,
): Promise<void> {
  await store.put<DiffState>(`diff:${league}`, { seq, snaps: [...snaps] }, 6 * 3600);
}
