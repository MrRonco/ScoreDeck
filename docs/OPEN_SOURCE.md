# Open source — what changes

ScoreDeck is intended for public release, like
[AirRadar](https://github.com/MrRonco/AirRadar). This document records what that
decision changes in [`PLAN.md`](PLAN.md). Read it alongside the plan, not
instead of it.

**The core architecture survives intact.** Five score models, a proxy in front
of the device, the 48-game cap, the threading contract, the alert budget — none
of that moves. What changes is *who runs the proxy*, *what may be committed to
the repo*, and *how a stranger gets the firmware onto a board*.

---

## 1. Logos — the change that actually matters

[`PLAN.md §3`](PLAN.md) has `tools/build-logos.ts` fetching from
`a.espncdn.com` and writing binaries into `assets/logos/`, served by the Worker.
In a private repo that is fine. In a public one it is the single most likely
cause of a takedown.

Those files would be **derivative works of ESPN-hosted image assets carrying
NHL, NFL, MLB, NBA, NCAA and football club trademarks**. The Premier League in
particular runs a serious enforcement operation. Roughly 3,000 club and
franchise marks committed to a public GitHub repo is not a grey area.

### The rule

**Logo binaries are never committed and never distributed by this project.**

- `assets/logos/` goes in `.gitignore`, with a `README` in the empty directory
  explaining why it is empty.
- `tools/build-logos.ts` ships in the repo. It runs on **the user's machine at
  their deploy time**, populating **their own** Worker's assets. The project
  distributes the code that fetches artwork; it does not distribute the artwork.
- `THIRD-PARTY-NOTICES.md` gets an explicit section stating that team names,
  abbreviations and logos are the trademarks of their respective owners, that no
  logo assets are included in this distribution, and that the build tool fetches
  them for personal use.
- The **web flasher images must not contain logos either.** They are the one
  binary the project does distribute (AirRadar already tracks
  `flasher/*-merged.bin` deliberately), so nothing trademark-encumbered may be
  compiled in.

### The fallback is a design win, not a consolation

The device needs to work with zero logos — on first boot, on a fresh Worker,
and for anyone who skips the logo build. So: **a generic team glyph — the
abbreviation set in the team's primary colour on a rounded square.**

```
┌──────┐   ┌──────┐        Colours are facts about a team,
│ TOR  │   │ LIV  │        not artwork. The proxy already
└──────┘   └──────┘        sends `color` in every `Side`.
 #00205B    #C8102E
```

This is genuinely better than a logo-or-nothing design: the Board is never
empty, the first paint needs no network round-trip, and the 6.9 KB-per-team
cache becomes a progressive enhancement rather than a dependency. Text team
names and abbreviations are nominative use and carry none of the same risk.

### Player headshots — the same rule, and it matters more

The player sheet ([`UI.md`](UI.md) §5) shows a 68×68 headshot built from
`a.espncdn.com/i/headshots/{league}/players/full/{id}.png`. Everything above
applies, and then some: **headshots are personal likenesses**, licensed through
the players' associations rather than the leagues — a stricter category than team
marks, and one where the subject is an individual person.

- `assets/players/` is gitignored alongside `assets/logos/`.
- Built only on the user's machine, into their own Worker, scoped to the rosters
  of teams they follow (~500 players).
- **Never in a release binary.** The flasher images stay clean.
- The fallback is the jersey-number badge the sheet already draws, so a build
  that was never run degrades to something deliberate.

Any published screenshot of the player sheet — README, artifact, release notes —
uses a silhouette placeholder rather than a real player's photograph.

---

## 2. The proxy becomes deploy-your-own

The original plan said to keep the proxy private to your own devices and not
publish it as a service. **That instruction turned out to be load-bearing in a
way it did not anticipate**, because open-sourcing the firmware means strangers
flash it — and if it ships pointing at your Worker, it is your service.
([`PLAN.md §2`](PLAN.md) now records the resolved position.)

### The arithmetic

Cloudflare Workers free tier is 100,000 requests/day. A device polls at 12 s
while a followed team is live and far slower otherwise:

| Usage | Requests/device/day | Devices before the free tier dies |
|---|---:|---:|
| Typical (4 h live, rest idle) | ~1,800 | **~55** |
| Heavy (polling at 12 s all day) | 7,200 | **~14** |

So a modestly successful repo exhausts a free Worker somewhere between **14 and
55 users** — and the failure mode is that *everyone's* device breaks at once,
including yours. Beyond cost, running the shared instance makes you the entity
redistributing a third party's data at scale, which is the profile that attracts
a letter. A hobby project should not carry that.

### The model

**Every user deploys their own proxy.** Two supported paths, one codebase:

1. **Deploy to Cloudflare button** in the README — one click, their account,
   their free tier, their `wrangler secret` for the bearer token. Their 100k/day
   is now serving one device instead of five hundred.
2. **`docker compose up`** on a Pi, NAS or homelab box, for people who don't
   want a cloud account. Hono runs unmodified on Node; the `Store` interface
   from `PLAN.md §3` swaps the Cache API for a local LRU. This path also serves
   the AirRadar crowd, who already have a Pi running a feeder.

The firmware ships with **an empty proxy URL**. Onboarding requires it — there
is no default to accidentally hammer. That is a deliberate friction of about
five minutes, and it is the price of the project not having an operator.

> **Recommendation: do not run a community instance**, even a best-effort
> rate-limited one. It converts a repo you maintain into a service you operate:
> uptime expectations, abuse handling, a bill that scales with your own
> popularity, and legal exposure that a "no SLA" notice in a README does not
> actually shed. If you want to lower the barrier, spend the effort on making
> the Deploy button flawless instead.

---

## 3. Data sources must be pluggable

A public project married to one undocumented endpoint is fragile in a way a
private one is not. If ESPN changes shape or starts blocking, every user's
device goes dark simultaneously and the repo reads as abandoned while you debug.

Define a `SportsSource` interface in Phase 1 — cheap now, expensive to retrofit:

```ts
interface SportsSource {
  id: string;
  leagues(): LeagueDescriptor[];
  board(league: string, date: string): Promise<NormalizedGame[]>;
  game(league: string, id: string): Promise<GameDetail>;
  roster(league: string, teamId: string): Promise<Roster>;
  news(favorites: Fav[]): Promise<NewsItem[]>;
}
```

| Source | Status | Notes |
|---|---|---|
| `espn` | Default | Best coverage, undocumented, no key, no SLA |
| `thesportsdb` | Fallback | Documented, free key `123`, **30 req/min** — proxy caching is what makes it usable; patchier live data |
| *(community)* | — | api-football, balldontlie, league-official feeds |

Two benefits beyond resilience: it makes "add a data source" an excellent
first contribution, and it lets users in regions ESPN covers poorly pick
something better.

---

## 4. Licensing

AirRadar's pattern is already correct and should be carried over wholesale: a
`LICENSE`, a `LICENSES/` directory with full texts, and a
`THIRD-PARTY-NOTICES.md` that is explicit about the statically-linked libraries
in the distributed binaries.

| Component | Recommendation | Why |
|---|---|---|
| **Firmware** (C++) | **GPL-3.0-or-later** | Matches AirRadar. All deps are compatible — LVGL MIT, LovyanGFX BSD-2, ArduinoJson MIT, arduino-esp32 LGPL-2.1, ESP-IDF Apache-2.0 — and AirRadar has already proven the analysis. |
| **Proxy** (TypeScript) | **AGPL-3.0-or-later** — *decided* | GPL's copyleft does not reach across a network: someone can take the proxy, host it as a paid service, and keep every improvement closed. AGPL closes that. Given the whole design puts the intelligence in the proxy, that is where the value is. |
| **Generated fonts** | OFL-1.1 | `font_*.c` files are not GPL. AirRadar already documents this. |
| **Docs and mockups** | CC-BY-SA-4.0 | Optional, but `INHERITED_RULES.md` is the kind of document people quote. |

**The AGPL trade-off, accepted knowingly:** it will deter some corporate
contributors and some users who have a blanket AGPL ban. That cost is worth
paying here, because the proxy is the whole design and a closed hosted fork is
the realistic way this project gets taken rather than joined.
**Two licences in one repo is normal** — mark it clearly with a
`LICENSE` at the root plus `proxy/LICENSE`, and state the split in the README's
first screen.

Add a `NOTICE`-style paragraph making the ESPN relationship unambiguous: this
project is **not affiliated with or endorsed by ESPN or any league**, it calls
publicly-reachable endpoints, and users are responsible for their own use.

---

## 5. Hardware stops being one board

Strangers own variants, and the plan currently hardcodes one. Split the pin map
and panel timings out of `config.h` into `boards/`:

| Board | Status | Note |
|---|---|---|
| `waveshare_7` | Reference | The AirRadar unit. Everything in [`HARDWARE.md`](HARDWARE.md). |
| `waveshare_7b` | Community | Different panel driver and touch controller — needs an owner to validate. |
| `waveshare_7c` | Community | **Has the ES8389 audio codec.** |

The 7C variant is worth calling out: [`HARDWARE.md`](HARDWARE.md) rules out
audio alerts because the reference board has no speaker. On a 7C that
restriction disappears, so **a goal horn becomes a real optional feature** —
compile-time gated, off by default, and one of the more appealing reasons for
someone to contribute a board port.

Board support is also the right place to say no gracefully. Ship one validated
board, define the interface, and let contributors bring the rest with a "tested
on hardware" requirement before merge.

---

## 6. The flashing story becomes mandatory

Nobody installs `arduino-cli` to try a stranger's hobby project. AirRadar
already has `flasher/` with an ESP Web Tools manifest, and for ScoreDeck this
is not a nice-to-have — it is the difference between users and stargazers.

- ESP Web Tools page on GitHub Pages, merged `.bin` attached to GitHub Releases.
- **Move it into Phase 0** and treat "a stranger can flash this from Chrome" as
  a gate, not a later polish item.
- GPL-3.0 obliges you to offer corresponding source for distributed binaries;
  linking the tagged release commit satisfies it. AirRadar's notices already
  cover the static-linking obligation — copy that text.

---

## 7. Tests and CI — and what is honestly testable

Your global rules ask for 80% coverage. On embedded that is achievable for some
of this codebase and dishonest for the rest, so split it explicitly:

| Layer | Testable | How |
|---|---|---|
| Proxy | **Yes, target 85%** | Vitest + recorded fixtures. Normalizers, diff engine and the registry are pure functions. |
| `core/` logic | **Yes, target 80%** | Compile the pure logic — score diffing, board sort and the 48-cap, situation bit-packing, status formatting — for the **host** with a native test target. No Arduino headers. This is the highest-value thing in the whole test plan and it needs the code designed for it from day one. |
| `net/`, `svc/` | Partially | Parser tests against fixtures; the transport is integration-only. |
| `ui/`, `hal/` | **No** | Not unit-testable in any useful sense. Cover with a manual pre-release checklist in `CONTRIBUTING.md`. |

**Fixture caution:** do not commit a 1.2 MB golf payload. Trim fixtures to two
or three events — enough to exercise a normalizer, small enough to be plainly
interoperability testing rather than republishing someone's database.

CI on every PR: `arduino-cli compile` for each board, `tsc --noEmit` plus
vitest for the proxy, the native core tests, and **gitleaks**. A public repo
where contributors paste config makes secret scanning mandatory rather than
tidy.

---

## 8. Security disclosure

The device runs an HTTP server on someone's LAN, and AirRadar found exactly the
bug class this attracts — a DNS-rebinding-defeatable CSRF guard, and a blank
secret field silently erasing a stored key (both in
[`INHERITED_RULES.md`](INHERITED_RULES.md) §20–21).

Public means someone will read that code adversarially, which is a *good*
outcome as long as there is somewhere to send the finding.

- `SECURITY.md` with a contact and a stated response window.
- Enable GitHub private vulnerability reporting.
- A short threat model in the docs: trusted LAN, untrusted internet, a proxy
  token that is a shared secret rather than an identity, and no PII on the
  device beyond Wi-Fi credentials and a team list.

---

## 9. Repo scaffold

AirRadar has a good `LICENSE`, `LICENSES/`, `THIRD-PARTY-NOTICES.md` and a
thoughtful `.gitignore` that deliberately excludes internal design docs — carry
all of that. What it does not have, and what a repo public from day one should
start with:

```
.github/workflows/ci.yml         compile · typecheck · test · gitleaks
.github/workflows/release.yml    tag → build → merged.bin → Release
.github/ISSUE_TEMPLATE/          bug (with board + firmware version) · league request
CONTRIBUTING.md                  build setup, the manual UI checklist, board-port bar
SECURITY.md                      disclosure policy
CODE_OF_CONDUCT.md               Contributor Covenant
docs/DEPLOY.md                   the Deploy button and the docker path
proxy/LICENSE                    AGPL, if the split above is taken
assets/logos/README.md           why this directory is empty
```

Carry AirRadar's `.gitignore` instinct too: working mockups and candid internal
review documents stay local. `INHERITED_RULES.md`, though, should be **public** —
it is the most useful thing either repo contains for anyone bringing up this
panel, and publishing it is most of why the project would be worth finding.

---

## 10. The name — **ScoreDeck** (decided)

The goal was a name with near-zero GitHub hits — unique, ownable, findable.
AirRadar already meets that bar at **18** matches; "ScoreBoard" fails it badly.
Measured 2026-08-09:

| Query | Repos matching | Notable |
|---|---:|---|
| `airradar` | **18** | the bar |
| `scoreboard` | **29,316** | — |
| `scoreboard in:name` | — | `SeniorZhai/ScoreBoard` **336★** already holds the exact name |
| **`scoredeck in:name`** | **5** | all ≤2★ |

The count was never the worst of it. The two most-starred `scoreboard` results
are **direct neighbours in this exact niche** —
`MLB-LED-Scoreboard/mlb-led-scoreboard` (710★) and
`riffnshred/nhl-led-scoreboard` (439★), hobbyist LED sports scoreboards. Under
that name this project would have read as an unnamed variant of those two rather
than its own thing. (They are also genuinely useful prior art — worth reading
before Phase 3.)

### What ScoreDeck collides with, precisely

Checked so there are no surprises later:

| Surface | Status |
|---|---|
| `github.com/MrRonco/ScoreDeck` | **Free** — this is the one that matters |
| GitHub repos named `scoredeck` | 5, all ≤2★, none in embedded or sports hardware |
| GitHub **org** `scoredeck` | **Taken** (`scoredeck/nba`, 1★) — so `github.com/scoredeck` is unavailable; use `MrRonco/ScoreDeck` |
| `scoredeck.io` | **Dead** — `ENOTFOUND`. `eyev/scoredeck` is the client for that defunct product |
| npm `scoredeck` | **Free** (404) |

No registered trademark surfaced, and the nearest prior use is an expired
domain. Good enough for a hobby project; not a legal opinion.

The reading holds up: *deck* as in flight deck or dashboard — an instrument
surface you glance at — paired with a domain noun, which is exactly AirRadar's
cadence.

Names avoided for trademark rather than collision reasons: *GameDay* (ESPN's
College GameDay), *RedZone* (NFL RedZone), *Jumbotron* (Sony), *Bleacher*
(Bleacher Report), *ScoreVision* (an existing scoreboard manufacturer). Also
ruled out on collisions: *Klaxon* (264 repos, `cbeust/klaxon` at 1,862★),
*Matchday* (754), *Touchline* (185).

**Baked-in identifiers**, all settled now rather than during Phase 0:

| Where | Value |
|---|---|
| mDNS hostname | `scoredeck.local` |
| NVS namespace | `"sdeck"` |
| Sketch / firmware dir | `firmware/ScoreDeck/ScoreDeck.ino` |
| Worker subdomain | `scoredeck.<account>.workers.dev` |
| Flasher URL | `mrronco.github.io/ScoreDeck/flasher/` |
| Release binary | `scoredeck-merged.bin` |

---

## 11. Revised phase order

Only the front of the plan moves here. [`PLAN.md §9`](PLAN.md) is the current
phase list — it also carries the `.5` interface phases added after the design
review, which are independent of everything in this document.

| # | Change |
|---|---|
| **0** | **+** licence and notices scaffold, CI, gitleaks, board abstraction, **+ the web flasher as a gate**, **+ the fallback team glyph** |
| **1** | **+** the `SportsSource` interface around the ESPN adapter (not a later refactor) |
| **1.5** | **New — deploy story.** Deploy-to-Cloudflare button, `docker-compose.yml`, `docs/DEPLOY.md`, secret handling. The repo is not publishable without it. |
| **2** | Logo pipeline is now a **user-run build step**, outputs gitignored, fallback glyph already shipping |
| 3–8 | Unchanged |

Publish the repo at the end of Phase 1.5 rather than at v1.0. A public repo with
a working proxy, a flashable binary and an honest "the Board screen is next"
README attracts the board ports and league adapters you want, and does it while
the architecture can still absorb them.
