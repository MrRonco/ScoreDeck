// The wire contract. Both firmware and proxy are built against this file.
// Field names are short on purpose — every byte crosses a 3 KB budget.

/** How a score-in-progress is described. The firmware implements five renderers. */
export const SM = {
  CLOCK: 0,
  INNING: 1,
  SET: 2,
  LEADERBOARD: 3,
  GRID: 4,
} as const;
export type ScoreModel = (typeof SM)[keyof typeof SM];

export const GS = { PRE: 0, LIVE: 1, FINAL: 2 } as const;
export type GameState = (typeof GS)[keyof typeof GS];

/** One side of a matchup. */
export interface Side {
  /** Team abbreviation, <=4 chars. "TOR" */
  a: string;
  /** Short display name, <=19 chars. "Maple Leafs" */
  n: string;
  /** Score. Absent before first score in some sports; always a number here. */
  s: number;
  /** Primary colour as 0xRRGGBB. Content, not chrome — see UI.md §2. */
  c: number;
  /** Context chip: record, seed, table position, pitcher. <=9 chars. "21-6-4" */
  r?: string;
  /** AP rank / seed. 0 or absent = unranked. */
  k?: number;
  /** ESPN team id, for the team screen. */
  id: string;
}

/** A game as the device models it. Hard cap 48 per board — see PLAN.md §4. */
export interface Game {
  /** ESPN event id. */
  i: string;
  /** League slug, matches registry key. "nhl" */
  l: string;
  /** Score model. */
  m: ScoreModel;
  /** State. */
  g: GameState;
  away: Side;
  home: Side;
  /** Short status. "3rd 04:21" | "Bot 7" | "FT" | "7:00 PM". <=15 chars. */
  st: string;
  /** Period / inning / set number. */
  p?: number;
  /**
   * Packed situation, sport-dependent:
   *   INNING  bit0-2 bases (1st,2nd,3rd) | bit3-4 outs | bit5 top-half
   *   CLOCK   bit0 home has possession   | bit1 red zone | bit2 power play
   */
  sit?: number;
  /** Kickoff, unix seconds UTC. */
  t: number;
  /** Region-resolved broadcast. "SN" | "Sky Sports". <=9 chars. Empty = unknown. */
  b?: string;
  /** Home win probability 0-100. Absent = unavailable. */
  wp?: number;
  /** True when home leads. Drives which side the edge light sits on. */
  lh?: boolean;
  /** Involves a followed team — drives sort order and alerting. */
  f?: boolean;
}

/** A scoring event the device may raise as an alert takeover. */
export interface ScoreEvent {
  /** Monotonic per-proxy. Device stores the last seen in NVS. */
  q: number;
  /** Event id this belongs to. */
  i: string;
  l: string;
  /** Sport verb. "GOAL" | "TOUCHDOWN" | "HOME RUN" | "THREE" | "TRY" */
  v: string;
  /** Scoring team abbreviation. */
  a: string;
  /** Scoring team colour 0xRRGGBB. */
  c: number;
  /** Scorer, already trimmed. "Auston Matthews" */
  w?: string;
  /** Detail line. "asst. Nylander, Rielly" */
  d?: string;
  /** Score after the event, away then home. */
  s: [number, number];
  /** Status at the time. "3rd · 04:21" */
  st: string;
}

/** GET /v1/state */
export interface StateResponse {
  /** Wire schema version. Bump on any breaking change. */
  v: 1;
  /** Server unix seconds — the device's clock source. */
  now: number;
  /** Games, already sorted favourite → live → soonest and truncated. */
  games: Game[];
  /** Events newer than the device's `seq`, oldest first. */
  ev: ScoreEvent[];
  /** Highest sequence the proxy currently holds. */
  seq: number;
  /** Per-league live counts for the top-bar strip. */
  lg: { l: string; n: number }[];
  /** Seconds until the device should poll again — the proxy owns cadence. */
  next: number;
  /** Set when upstream data is stale; the device says so rather than lying. */
  stale?: boolean;
}

/** Generic table — standings and stat comparisons both use this shape. */
export interface Table {
  cols: string[];
  rows: (string | number)[][];
  /** Row indices after which to draw a labelled cut line. */
  cuts?: { after: number; label: string }[];
}

export interface LeagueDef {
  /** Registry key and wire value. */
  slug: string;
  /** ESPN path segment. "hockey/nhl" */
  path: string;
  model: ScoreModel;
  /** Display label for the top-bar strip. */
  label: string;
  /** Sport family — picks the standings column set and the alert verb. */
  family: 'hockey' | 'football' | 'basketball' | 'baseball' | 'soccer' | 'other';
}
