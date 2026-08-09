import { SM, type LeagueDef } from './types.ts';

/**
 * Adding a league is a row here plus a logo build. No firmware change.
 * Every path verified live against
 * site.api.espn.com/apis/site/v2/sports/{path}/scoreboard on 2026-08-09.
 */
export const LEAGUES: LeagueDef[] = [
  // ── CLOCK ────────────────────────────────────────────────────────────────
  { slug: 'nfl',    path: 'football/nfl',                          model: SM.CLOCK, label: 'NFL',   family: 'football' },
  { slug: 'ncaaf',  path: 'football/college-football',             model: SM.CLOCK, label: 'NCAAF', family: 'football' },
  { slug: 'nba',    path: 'basketball/nba',                        model: SM.CLOCK, label: 'NBA',   family: 'basketball' },
  { slug: 'wnba',   path: 'basketball/wnba',                       model: SM.CLOCK, label: 'WNBA',  family: 'basketball' },
  { slug: 'ncaam',  path: 'basketball/mens-college-basketball',    model: SM.CLOCK, label: 'NCAAM', family: 'basketball' },
  { slug: 'ncaaw',  path: 'basketball/womens-college-basketball',  model: SM.CLOCK, label: 'NCAAW', family: 'basketball' },
  { slug: 'nhl',    path: 'hockey/nhl',                            model: SM.CLOCK, label: 'NHL',   family: 'hockey' },
  { slug: 'ncaawh', path: 'hockey/womens-college-hockey',          model: SM.CLOCK, label: 'NCAAW H', family: 'hockey' },

  // Soccer — same model, one row each.
  { slug: 'eng.1',  path: 'soccer/eng.1',           model: SM.CLOCK, label: 'EPL',    family: 'soccer' },
  { slug: 'esp.1',  path: 'soccer/esp.1',           model: SM.CLOCK, label: 'LaLiga', family: 'soccer' },
  { slug: 'ger.1',  path: 'soccer/ger.1',           model: SM.CLOCK, label: 'Bund',   family: 'soccer' },
  { slug: 'ita.1',  path: 'soccer/ita.1',           model: SM.CLOCK, label: 'SerieA', family: 'soccer' },
  { slug: 'fra.1',  path: 'soccer/fra.1',           model: SM.CLOCK, label: 'Ligue1', family: 'soccer' },
  { slug: 'ucl',    path: 'soccer/uefa.champions',  model: SM.CLOCK, label: 'UCL',    family: 'soccer' },
  { slug: 'uel',    path: 'soccer/uefa.europa',     model: SM.CLOCK, label: 'UEL',    family: 'soccer' },
  { slug: 'uwcl',   path: 'soccer/uefa.wchampions', model: SM.CLOCK, label: 'UWCL',   family: 'soccer' },
  { slug: 'mls',    path: 'soccer/usa.1',           model: SM.CLOCK, label: 'MLS',    family: 'soccer' },
  { slug: 'nwsl',   path: 'soccer/usa.nwsl',        model: SM.CLOCK, label: 'NWSL',   family: 'soccer' },

  // ── SET ──────────────────────────────────────────────────────────────────
  { slug: 'atp',  path: 'tennis/atp', model: SM.SET, label: 'ATP', family: 'other' },
  { slug: 'wta',  path: 'tennis/wta', model: SM.SET, label: 'WTA', family: 'other' },

  // ── LEADERBOARD ──────────────────────────────────────────────────────────
  { slug: 'pga',  path: 'golf/pga',   model: SM.LEADERBOARD, label: 'PGA',  family: 'other' },
  { slug: 'lpga', path: 'golf/lpga',  model: SM.LEADERBOARD, label: 'LPGA', family: 'other' },

  // ── GRID ─────────────────────────────────────────────────────────────────
  { slug: 'f1',   path: 'racing/f1',  model: SM.GRID, label: 'F1', family: 'other' },

  // ── INNING ───────────────────────────────────────────────────────────────
  { slug: 'mlb',    path: 'baseball/mlb',               model: SM.INNING, label: 'MLB',  family: 'baseball' },
  // Softball lives under baseball/ — softball/college-softball returns 400.
  { slug: 'ncaasb', path: 'baseball/college-softball',  model: SM.INNING, label: 'NCAA SB', family: 'baseball' },
];

const BY_SLUG = new Map(LEAGUES.map((l) => [l.slug, l]));

export function league(slug: string): LeagueDef | undefined {
  return BY_SLUG.get(slug);
}

/** The verb the alert takeover shouts, by sport family and points scored. */
export function scoreVerb(family: LeagueDef['family'], delta: number): string {
  switch (family) {
    case 'hockey':
    case 'soccer':
      return 'GOAL';
    case 'football':
      return delta >= 6 ? 'TOUCHDOWN' : delta === 3 ? 'FIELD GOAL' : 'SCORE';
    case 'basketball':
      return delta === 3 ? 'THREE' : delta === 2 ? 'BUCKET' : 'FREE THROW';
    case 'baseball':
      return delta >= 4 ? 'GRAND SLAM' : delta > 1 ? 'HOME RUN' : 'RUN';
    default:
      return 'SCORE';
  }
}
