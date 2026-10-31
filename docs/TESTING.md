# Testing and review

How ScoreDeck is tested, reviewed and released. This is the methodology used to build
oob-manager (a Go web app), adapted to this repo's stack: ESP32-S3 firmware on LVGL, an SDL
desktop harness, a TypeScript proxy, a browser portal served from flash, a web flasher, a
container image and an Unraid template.

The principles do not change between projects:

- **Nothing ships on a single author's word.** The builder never reviews its own work.
- **A rule that matters gets a check.** If a rule can be enforced by CI, it is, and the check
  fails the build. A rule that is only written down will be broken.
- **CI never talks to the real world.** No live ESPN calls, no real panel, no production data.
  Recorded fixtures and fakes only.

Section 1 is an honest account of what exists today. Section 2 is the target, per component.
Sections 3–5 are the review, release and CI-runner rules. Section 6 is the order to adopt it in.

---

## 1. Current state (main @ 258e046, 2026-10-31)

### 1.1 `ci.yml` — on every pull request and every push to main

Top-level `permissions: contents: read`. Every action is pinned to a full commit SHA with the
major tag in a comment. Four jobs, all on `ubuntu-latest`:

| Job | What it does | What it does not do |
|---|---|---|
| **proxy** | Node 22, `npm ci`, `tsc --noEmit` (strict, `noUncheckedIndexedAccess`), `npm test` | No coverage, no lint/format, no dependency scan. Node version is `'22'`, not exact. |
| **firmware** | `arduino-cli` with esp32 core **3.3.10**, LVGL **8.3.11**, LovyanGFX **1.2.25**, ArduinoJson **6.21.6** (all pinned), places `lv_conf.h`, compiles for the ESP32-S3 FQBN with `--warnings default` | No size budget, no artifact, no static analysis, no host tests. The `arduino-cli` version itself is not pinned. |
| **secrets** | gitleaks over the full history (`fetch-depth: 0`) | No allowlist config for documented placeholders. |
| **assets** | Fails if logo or headshot binaries are tracked under `assets/` (the trademark rule in [`OPEN_SOURCE.md`](OPEN_SOURCE.md) §1) | Inline shell, not a reusable policy test. |

The proxy suite is real and worth crediting: **31 tests** with `node:test` and a stubbed
`fetch` (no live calls). 11 route tests cover the score-event diff (one event, exactly once,
across app instances), the device payload budget, bearer enforcement, **fail-closed without
`SD_TOKEN`**, a wrong token still being a 401 through the constant-time compare, CORS scoped
to the reference routes, preflight not bypassing auth, stale-not-empty on upstream failure,
and rejection of unknown leagues and bad favourites. 20 normaliser tests cover the five score
models, broadcast resolution, multi-day events and story clamping.

### 1.2 `image.yml` — on push to main (`proxy/**`) and on `v*` tags

Builds `proxy/` for `linux/amd64` and `linux/arm64` and pushes to
`ghcr.io/<owner>/scoredeck-proxy`, tagged by branch, semver and `latest` (default branch).
Job-level `packages: write`.

Gaps:

- It **does not wait for CI**. A main push that fails `ci.yml` still publishes an image.
- No Trivy scan, no SBOM, no cosign signature, no explicit provenance attestation.
- The base image `node:22-alpine` is a floating tag, not a digest.
- `latest` is the tip of main, and the Unraid template follows `latest`. **Merging to main is
  a deploy** to every Unraid install on its next update.

### 1.3 The desktop harness (`desktop/`) — local only

The strongest test asset in the repo, and none of it runs in CI.

- **What it is.** `make` compiles the real `ui/*.cpp`, `core/state.cpp` and generated fonts
  against SDL, with the firmware's `lv_conf.h` used **verbatim**. `fakes.cpp` stubs only the
  network, flash and panel ("stub the plumbing, keep the logic").
- **Scenarios.** `scenarios.cpp` holds the hard-to-summon cases: a 48-game Saturday,
  three-digit scores, accented names, a stale feed, no proxy, tennis/golf/F1. `make shots`
  renders 11 scenarios and `make screens` renders 11 screens to BMP, headless, with
  `--settle` to wait out one-shot animations. The output is gitignored and reviewed by eye.
  There is no baseline and no comparison.
- **`make lint` (font lint).** It walks the live LVGL object tree on every screen and
  scenario, and fails if any visible label holds a codepoint its font cannot draw. It exists
  because the same bug shipped six times.
- **Not deterministic yet.** `millis()` reads a steady clock and the shims call
  `time(nullptr)`, so clocks, `AS OF` stamps and relative times change between runs. The LVGL
  path defaults to a macOS Arduino folder.

On the `design-audit-2` branch (pushed, not yet merged to main):

- **`lint_invariants.py`** asserts 11 source rules that used to live in comments: colour
  literals only in `theme.cpp`, no hex in recolour strings, no chromatic token at partial
  opacity, one radius family, press/inert surfaces, button style reset, glass fill, accent
  meaning, final-score rung, dead tokens and logo variants. `make lint` runs it.
- **`lint_baseline.txt`** is a **shrink-only baseline**. The lint fails on any violation over
  the baseline and also on any entry that has been fixed but not removed. This is exactly the
  baseline pattern this document asks for elsewhere, and it is reused below.
- **`kitsweep.cpp`** is a one-off contrast sweep of kit colours through the real solver. It
  is not wired into the Makefile.

### 1.4 Other things that exist

- **`docs/OPEN_SOURCE.md` §7** already sets the coverage split: proxy 85 %, `core/` 80 %
  through a host build, `net/`/`svc/` parsers against fixtures, `ui/`/`hal/` through the
  harness plus a manual checklist. This document keeps those numbers. (The `CONTRIBUTING.md`
  checklist it mentions does not exist yet.)
- **The August 2026 security review** ([`SECURITY-REVIEW-2026-08.md`](SECURITY-REVIEW-2026-08.md)):
  1 critical, 4 high, 8 medium and 4 low findings, all remediated. That was the first round of
  the review loop in section 3. Its proxy-side fixes have regression tests. Its firmware-side
  fixes have none, because there is no host test build:
  - OTA needs a password, plus Digest auth with lockout;
  - the `Host` check against DNS rebinding;
  - the relay path allowlist and the response size caps;
  - TLS verification.
- **The portal** is authored as HTML and generated into `svc/portal.h` and `svc/portal_csp.h`
  by `tools/embed-portal.mjs`, which also pins the inline script by CSP hash. `portal/mock.mjs`
  serves it with a stub device API. Nothing checks that the generated headers are in sync, and
  there are no browser tests.
- **The flasher** vendors `esp-web-tools` 10.4.0 with an SRI hash and a strict CSP.
  `SECURITY.md` lists the SHA-256 of both committed images. They match today, but:
  - the images were committed by hand;
  - firmware source has changed since, and `SD_VERSION` is still `0.2.0`;
  - so nothing ties a published binary to the commit that built it.
- **GitHub Pages** serves the root of main (legacy mode). **Merging to main publishes the
  flasher**, too.
- **Not in place:** branch protection on main, Renovate or Dependabot, actionlint, shellcheck,
  coverage, dependency scans, container scans, browser tests, and repo-policy tests beyond the
  trademark check. There was no `CLAUDE.md` before this change.

---

## 2. Target gates

Every gate below is **blocking on main** once adopted. A gate is never `continue-on-error`, and
a flaky test is a bug to fix, never something to retry (section 2.9).

### 2.1 Firmware and desktop harness

**Host unit tests for the logic layer, with sanitizers.**

- Build pure logic for the host (clang/gcc, `-fsanitize=address,undefined`, `-fno-sanitize-recover`).
  Use the same shim approach the harness already uses. ArduinoJson is header-only and builds
  on the host unchanged.
- Test targets, in priority order:
  1. **Feed parsing.** Today the board, game, standings, lineup, news and catalog parsers live
     inside `net/api.cpp`, interleaved with HTTP. Extract each into a pure function
     (`bytes → struct`), so it can be tested and fuzzed without a transport. This refactor is
     the prerequisite for most of this section.
  2. **Portal input handling** in `svc/web.cpp`: `hostAllowed`, `originAllowed`, `secFetchOk`,
     `validFavs`, `proxyHostSafe`, `jsonField`/`jsonInt`, `applySecret` (blank keeps, `-`
     clears), and the `/api/relay` path allowlist. These are security controls with no tests
     today. Extract them to a module with no `WebServer` dependency.
  3. **Time and state machines** in `core/state.cpp`: quiet hours across midnight,
     `clockFormat` 12/24 h, `tzApply`/`tzIndexOf`, favourites ordering (`favAt`/`favRemove`/
     `favMoveUp`), `situationText` in compact and long form, `boardFollows`, `lastGoodClock`,
     the TLS gate decision, and poll cadence.
- A small vendored single-header framework (pinned version and checksum) or plain asserts.
  No framework that needs a package manager on the host.

**Coverage on the logic layer.**

- At least **80 % lines per module** for `core/` and the extracted parse and validate modules,
  measured with `llvm-cov` or `gcovr`, and enforced in CI.
- **Excluded, with the reason written next to the threshold:**
  - `ui/`: covered by the font lint, the invariant lint and the visual baselines;
  - `hal/` and the transport half of `net/`/`svc/`: hardware-only, covered by the device
    checklist and the deploy verification.

**ESP32 compile in CI** (exists). Extend it:

- pin the `arduino-cli` version;
- compile with `--warnings all`, and track new warnings with a shrink-only baseline;
- upload the `.elf` and `.map` as artifacts.

**Size budgets.** Fail the firmware job when:

- the app image passes a fixed share of the 3 MB `app3M` partition (today 1.67 MB, 52 %;
  suggested limit 75 %, raised only on purpose);
- static DRAM or IRAM use grows by more than a set number of KB over main's last green build.

Report the deltas in the job summary. Also fail when the embedded portal (`portal.h`) passes
the ~40 KB point at which `embed-portal.mjs` says gzip should be revisited.

**Static analysis with a baseline.**

- `cppcheck` over `firmware/ScoreDeck/src` (exclude generated `assets/` fonts and
  `svc/portal.h`).
- `clang-tidy` (`bugprone-*`, `cert-*`, `clang-analyzer-*`, a curated `performance-*`) using
  a `compile_commands.json` from the host build.
- Both use the `lint_baseline.txt` pattern: counts per rule and file, shrink-only, and fail
  on stale entries.

**Source invariants** (exists on `design-audit-2`). Run `make lint` in CI so both the font
lint and `lint_invariants.py` block. Add the checkable rules from
[`INHERITED_RULES.md`](INHERITED_RULES.md) as invariants:

- `LV_INV_BUF_SIZE` is 64 and the draw buffer is 800×30 in `lv_conf.h`/`config.h` (rules 6, 10);
- no `DynamicJsonDocument` in a function that calls `vTaskDelete(NULL)` (rule 13);
- every secret write goes through the blank-keeps guard (rule 20);
- no `setInsecure()` outside the explicit `tlsInsecure` path.

**Render budgets.** The harness flush handler already sees every flushed area. Assert two
things in CI:

1. After a scenario settles, a poll that delivers **identical data** flushes 0 px.
2. A one-score change flushes no more than a stated bound.

This turns rule 8 and the README's 248,144 px regression (64.6 % of the screen repainted
every minute for no visual change) into a failing test.

**Deterministic visual baselines from the scenarios.**

1. Render in a **pinned container**: a base image by digest, exact gcc and SDL2 versions,
   LVGL 8.3.11 fetched by tag and checked against a checksum, `SDL_VIDEODRIVER=dummy`. Set the
   Makefile's `LVGL` variable explicitly.
2. **Freeze time.** Add a harness flag for a fixed epoch and a virtual `millis()` that only
   advances in the settle loop. Without it, the idle clock and `AS OF` stamps make every run
   different.
3. Render every scenario × screen with a fixed `--settle`. `SDLOGO` stays unset, so baselines
   show the colour-badge fallback and never a trademarked mark.
4. Store baselines as lossless PNG under `desktop/baselines/`. Compare **pixel-exact**: this is
   integer software rendering into RGB565 with compiled-in bitmap fonts, so any difference is
   a real change. Only if a measured source of non-determinism cannot be removed, allow a
   documented pixel-count threshold.
5. Regenerate baselines **only** through a `workflow_dispatch` input
   (`update_baselines: true`). The job uploads the new set and a diff sheet as an artifact. A
   human reviews them, then commits them in a separate commit. Never regenerate locally and
   commit blind.

**Fuzzing.**

- libFuzzer targets (clang, with ASan/UBSan) for each extracted feed parser and for the portal
  input functions.
- Seed corpora come from the recorded fixtures (2.2).
- Each pull request runs every target for a short fixed time (e.g. 60 s). A nightly job on
  main runs them longer and stores the corpus as an artifact.
- Any crash becomes a regression test before it is fixed.

**The harness itself under sanitizers.** Build it with `SAN=1` and run `make lint shots
screens` in CI. This puts every real UI file under ASan/UBSan across all scenarios at almost
no cost.

### 2.2 Proxy (`proxy/`)

**Unit and integration tests against recorded fixtures.**

- `proxy/test/fixtures/` holds recorded ESPN responses: scoreboard, summary, standings,
  teams, news and story, for at least one league per score model (`CLOCK`, `INNING`, `SET`,
  `LEADERBOARD`, `GRID`).
- Trim each to two or three events, as `OPEN_SOURCE.md` §7 requires (never the 1.2 MB golf
  payload).
- The test `fetch` serves fixtures by URL and **throws on any URL it does not know**, so a
  test cannot reach the network by accident.
- Optional and non-blocking: a scheduled job that re-records fixtures and opens a PR when
  ESPN's shape changes. It never runs on pull requests.

**Coverage.** `node --test --experimental-test-coverage` with
`--test-coverage-lines=85 --test-coverage-branches=…`. Node 22 supports these thresholds, so
no new dependency is needed. `worker.ts` is tested with a stub `env` and scheduled event.

**Input validation tests.** Cover every route parameter:

- league slugs, ids and `:file` on `/v1/logo` and `/v1/head`, including path traversal:
  `..`, `%2e%2e%2f`, encoded backslashes, NUL;
- the favourites cap of 20 and the leagues cap of 12;
- `tz` and region values;
- oversized query strings.

An unknown value is rejected or ignored, never interpolated into an upstream URL. That is the
**SSRF** test: no route can make the proxy fetch a host other than ESPN's.

**Caching tests.**

- TTLs: board 20 s, teams 24 h.
- `MemoryStore`'s 2,000-key cap and its eviction order.
- `img` is never cached with a player payload.
- Request coalescing: N concurrent identical `/v1/state` requests must cause one upstream
  fetch per league.
- `guardedFetch`: the timeout and the 3 MB byte cap.

**Rate limiting.** The proxy has none today. It is LAN-only and token-gated, so a limit is a
product decision, not a given. If one is added, it gets tests: per-client limit, `429` with
`Retry-After`, and `/v1/health` never limited. The cache-stampede test above is required
either way, because it protects ESPN from this proxy.

**Lint and format.** `tsc --noEmit` exists. Add a linter and formatter as a new
dev-dependency, under the 14-day rule (section 2.8).

**Dependency scan.** `npm audit --omit=dev --audit-level=high`, plus `osv-scanner` on
`package-lock.json`. Fail on fixable high and critical findings.

**ZAP baseline.** The proxy serves HTTP, so ZAP runs against it:

1. Start the built container with a test token, and with an upstream that serves the fixtures,
   never ESPN. This needs a small test-only hook, e.g. a fixture directory env var that is
   refused when `NODE_ENV=production`.
2. Run the ZAP baseline scan, with the bearer header supplied through ZAP's replacer, so it
   scans authenticated responses as well as the 401/503 paths.
3. Rules are tuned in a committed `zap-rules.tsv`, with a reason on every `IGNORE`.

### 2.3 Portal (`portal/`)

**Generated header drift.** CI runs `node tools/embed-portal.mjs` and then
`git diff --exit-code firmware/ScoreDeck/src/svc/portal.h firmware/ScoreDeck/src/svc/portal_csp.h`.
A portal edit without a regenerated header fails.

**Playwright e2e against `mock.mjs`.** First, `mock.mjs` must send the **same CSP header the
firmware sends**, built from `portal_csp.h`, so that CSP violations in tests are meaningful.
Its data must use placeholders only (2.7). Then cover:

- **Keyboard paths.** Every one of the six hash views is reachable and operable by keyboard,
  with a visible focus ring.
- **Secrets.** The password and token fields are never populated from `/api/config`. Saving an
  unrelated setting sends no secret, and `-` sends the clear marker (INHERITED_RULES 20).
- **XSS regression for review finding H1.** A hostile catalog through `/api/relay` (e.g. a
  slug containing `"><img src=x onerror=…>`) renders as text, fires no CSP violation and
  stores nothing executable in `localStorage`.
- **Relay fallback.** It works when the proxy URL is unreachable, which is the mock's default.
- **Visibility polling.** The Board view polls only while the tab is visible.
- **Zero console errors** and **zero CSP violations** on every view.

**Accessibility and layout.**

- An **axe** scan of every view in **light and dark**, at **1280 px and 390 px**. Fail on
  serious and critical findings.
- **No horizontal scroll** at 320, 360 and 390 px.
- With `prefers-reduced-motion: reduce`, no transition or animation runs.
- Tap targets are at least 44 px (the README promises it, so test it).

**Visual baselines.** Optional: Playwright screenshots in the pinned Playwright container,
with the same regenerate-by-input rule as 2.1.

### 2.4 Flasher (`flasher/`)

**Playwright smoke.** The page loads with zero console errors and zero CSP violations, and the
`esp-web-install-button` custom element is defined. Headless Chromium has no Web Serial, so
also check that the unsupported-browser path renders a usable message rather than a dead
button.

**Axe** in light and dark, at 1280 and 390 px.

**Manifest and checksum tests** (plain Node, no browser):

- `manifest.json` is valid against the esp-web-tools manifest shape, every `parts[].path`
  exists, and the offsets and `chipFamily` are as expected;
- the manifest `version` equals `SD_VERSION` in `config.h` and, on a tag, the tag;
- the `integrity=` hash in `index.html` equals the SHA-384 of the vendored bundle;
- the SHA-256 values in `SECURITY.md` equal the committed `.bin` files;
- the vendored bundle can be rebuilt from the recipe in `vendor/README.md` (pinned
  `esp-web-tools` and `esbuild`). If the rebuild is not byte-reproducible, verify the npm
  tarball integrity instead and say so.

**Target:** release images are built by CI from the tag (section 4), never committed by hand.

### 2.5 Container image

On every pull request that touches `proxy/` (build, no push), and again in the release:

- **Trivy** image scan: `severity: HIGH,CRITICAL`, `ignore-unfixed: true`, `exit-code: 1`.
  Fixable high and critical findings fail; accepted ones go in `.trivyignore` with a reason
  and an expiry date.
- **SBOM** (SPDX or CycloneDX) generated from the built image and attached as an artifact,
  and as an attestation on release.
- **Non-root check.** `docker inspect` shows a non-root `User`.
- **Read-only check.** The container starts and passes `/v1/health` with `--read-only`,
  `--cap-drop ALL`, `--security-opt no-new-privileges` and a tmpfs `/tmp`. This proves the
  "needs no write access" comment in the Dockerfile.
- **Base image pinned by digest.** Renovate updates it.
- **Publish only after CI.** `image.yml` publishes only for a SHA whose `ci.yml` run passed,
  using `workflow_run` or one workflow with `needs:`.
- **Release signing.** cosign keyless signs the image **by digest**, with
  `actions/attest-build-provenance` and the SBOM attestation.
- **Owner decision:** whether `latest` should follow release tags rather than main, so that
  Unraid installs only receive releases.

### 2.6 Unraid template (`unraid/`)

- **XML validity:** `xmllint --noout` on every `*.xml`.
- **Template contract test:**
  - the `Repository` matches the image name `image.yml` publishes;
  - every `Config Target` is an env var or port the proxy actually reads (`SD_TOKEN`, `TZ`,
    `PORT`);
  - the `Icon` URL points to a file that is tracked. Today it points to `unraid/icon.png`,
    which does not exist.
- **Doc-matches-behaviour test.** Today the template's descriptions are out of date in two
  places:
  - the token help says an empty token "runs the proxy open", but the proxy fails closed;
  - the Overview says 20 leagues, the README says 26.
  A test that asserts these against the source keeps them from drifting.
- **Repo-policy test** (2.7) on `unraid/`: no personal paths, addresses, VLAN numbers or
  references to private scripts. `scoredeck-boot.sh` fails it today. Either make it generic
  (all values from variables) or move it out of the repo.

### 2.7 Across the repo

- **gitleaks** (exists). Add a `.gitleaks.toml` with an allowlist for the documented
  placeholder values, so that the scan stays strict for everything else.
- **actionlint** on `.github/workflows/*`. Add **zizmor** for workflow security (template
  injection, excessive permissions).
- **shellcheck** on `install.sh`, `tools/*.sh` and `unraid/*.sh`.
- **Repo-policy tests.** A small script with no new dependencies (e.g.
  `tools/policy/check.mjs` run by `node --test`). Each rule is a test, and current
  violations live in a shrink-only baseline:
  - **No personal identifiers:**
    - no private IPv4 addresses outside an allowlist of documentation placeholders
      (RFC 5737 ranges, and the existing `192.168.1.50` example);
    - no absolute home paths (`/Users/…`, `/home/…`) — today an include path on
      `design-audit-2` would fail this;
    - no real SSIDs, hostnames, MAC addresses or e-mail addresses beyond the noreply author.
    The specific identifier list is kept **outside the repo**, as a CI secret or a local file,
    so the check cannot leak it.
  - **No API keys or tokens** (gitleaks covers this), and no tracked `.env`, `.dev.vars` or
    `dev_defaults.h`.
  - **Trademark rule:** no logo or headshot binaries. The existing inline check moves here.
  - **Pins:** every `uses:` has a 40-character SHA, every workflow declares `permissions:`,
    and no workflow uses `pull_request_target`.
  - **Build consistency:** one FQBN everywhere. Today they disagree. `ci.yml`, `.fqbn` and
    `BUILD.md` use `CDCOnBoot=default,FlashMode=qio`; the README and `tools/restore-panel.sh`
    use `USBMode=hwcdc`. The owner picks the canonical one.
  - **Version consistency:** the core and library versions are identical in `ci.yml`,
    `BUILD.md` and the README. `SD_VERSION`, `manifest.json` and the release tag agree.
  - **Forbidden APIs:**
    - no `innerHTML`, `outerHTML`, `insertAdjacentHTML`, `document.write`, `eval` or inline
      `on*=` handlers in `portal/` and `flasher/`;
    - no `'unsafe-inline'` in a `script-src`;
    - no CDN `<script src>`.
  - **Docs match the code:** the README's scenario count and key list agree with
    `scenarios.cpp`. The "how to verify" section agrees with the release workflow.
- **Leak hunt before anything is published** (section 3.3). Committed screenshots are part of
  it: a policy test can check image metadata, but only a person can check what a screenshot
  shows.

### 2.8 Supply chain

- **Actions are pinned by SHA** (already done), with the tag in a comment. Pin runner images
  too (`ubuntu-24.04`, not `ubuntu-latest`).
- **Exact versions:** Node (e.g. `22.x.y`), `arduino-cli`, the esp32 core and libraries (done),
  `lv_font_conv`, Playwright and Trivy.
- **The 14-day rule.** A new dependency or tool version must be **at least 14 days old**.
  Verify a downloaded tool by checksum or signature. `tools/build-fonts.sh` currently fetches
  fonts from the `main` branch of `google/fonts`; pin a commit and check a SHA-256.
- **Renovate** with `minimumReleaseAge: "14 days"`. It covers:
  - npm (`proxy/`);
  - GitHub Actions digests;
  - the Dockerfile base digest;
  - a regex manager for the `arduino-cli` core and library versions in `ci.yml` and
    `BUILD.md`, kept in lockstep by the consistency test;
  - the vendored `esp-web-tools` version.
  Installing the Renovate app is an owner action.
- **Releases are built by CI** on GitHub-hosted runners, signed with cosign keyless, and carry
  provenance and SBOM attestations plus a `SHA256SUMS` file. A test keeps the "how to verify"
  section of `SECURITY.md` in sync with the release workflow.

### 2.9 Test-first and flaky tests

- Write the test, watch it fail, then make it pass. **Every bug fix starts with a test that
  reproduces it.** For UI bugs that means a scenario, a lint rule, a render-budget assertion
  or a baseline that shows the bug, as the font lint and the invariants already do.
- **Flaky tests are bugs.** Reproduce them under load (repeat runs, `stress`) and fix the
  timing assumption: poll with a deadline instead of sleeping, give generous limits, and make
  fakes fail the way the real thing fails. Never add retries. If a test exposes a real
  ordering bug in the product, fix the product.

---

## 3. Independent review

### 3.1 The loop

1. Work happens on a branch, one git worktree per parallel task.
2. When the builder says it is done, a **separate security reviewer** reads the diff. It
   ranks findings CRITICAL, HIGH, MEDIUM and LOW, each with file:line, a failure scenario and
   a fix. **It edits nothing.**
3. The builder fixes the findings **test-first**.
4. The **same reviewer re-checks** the fixes. Steps 3–4 repeat until **no CRITICAL, HIGH or
   MEDIUM** finding is open. A LOW finding is fixed, or written down with a reason.
5. A code-quality review runs alongside it. The builder never reviews its own work.

The August 2026 review and its remediation was this loop's first round. It is the bar to keep.

### 3.2 What counts as security-sensitive here

These changes always get the loop:

- **The proxy:**
  - token auth and the fail-closed default;
  - anything that builds an upstream URL (SSRF: a route parameter must never pick the host);
  - the `guardedFetch` timeout and byte caps;
  - cache keys and growth (cache poisoning, memory exhaustion);
  - CORS scoping;
  - the asset routes (path traversal).
- **The device web server and the portal** (`svc/web.cpp`, `portal/`):
  - Digest auth and lockout;
  - the `Host`/`Origin`/`Sec-Fetch` checks (DNS rebinding);
  - the `/api/relay` allowlist;
  - secret handling;
  - `/screen.bmp` (it shows whatever is on the glass);
  - anything that renders network strings;
  - the CSP and its script hash.
- **OTA** (`/update`): who may upload, and what is accepted. Today any image uploaded by an
  authenticated user is flashed. There is no image signature check, and the use of the ESP32
  app-rollback mechanism is not documented. Signed OTA, secure boot and rollback-on-failed-boot
  are owner decisions. Until they are made, any change to this path gets the loop.
- **`install.sh`.** It is piped from `curl` into `bash` and runs as the invoking user, which is
  root on Unraid and on many NAS boxes. It clones `main` unpinned, writes `.env` and starts
  containers. Pinning it to a release tag is part of the release work (section 4).
- **The flasher supply chain:**
  - the vendored bundle and its SRI hash;
  - the images and the manifest;
  - GitHub Pages serving main.
- **The firmware's TLS and response-size handling** (review findings H2 and M2), and any
  parser that reads the proxy's output.
- **CI workflows:** permissions, secrets, triggers, and the self-hosted runner selection
  (section 5).

### 3.3 Leak hunt before publishing

The repo is public and Pages serves it. So before anything is pushed to main, released, or
added to the docs, a separate pass checks for personal data:

- all text, fixtures and mock data (`portal/mock.mjs`, test fixtures);
- image metadata;
- **what the screenshots actually show** — SSIDs, LAN addresses, hostnames, proxy URLs.
  Several web-console screenshots are captured from a real device, so check every new one.

The private identifier list stays outside the repo. The first full leak hunt has open
findings; they are tracked privately, not in this file.

---

## 4. Integration and release

### 4.1 Integration

1. CI is green on the **branch tip**.
2. Merge with `git merge --no-ff -F <message-file>`.
3. Verify main's CI by the **full commit SHA** (`gh run list --commit <sha>`), not by "the
   latest run".
4. Tag `vX.Y.Z`. `SD_VERSION`, `manifest.json` and the tag agree (a policy test checks this).

**Merging to main is a deploy today.** Pages publishes the flasher from main, and `image.yml`
moves `latest`, which the Unraid template follows. Until section 4.3 is in place, treat every
merge to main as a release: it needs the owner's OK.

### 4.2 Release (tag-triggered, GitHub-hosted runners)

- **Firmware:**
  1. build the merged and OTA images from the tag;
  2. apply the size gate;
  3. write `SHA256SUMS`;
  4. sign with `cosign sign-blob` keyless and attach build provenance;
  5. attach everything to the GitHub Release.
- **Proxy image:** build, run Trivy and the read-only/non-root checks, push by digest, sign
  with cosign, and attach the SBOM and provenance attestations.
- **Release notes** include the hashes and the "how to verify" steps, kept in sync by a test.
- **Soak.** A release candidate soaks unchanged on the owner's panel and proxy, e.g. for 7
  days. A change touching portal auth, OTA, TLS or proxy auth restarts the soak.

### 4.3 Deploys, with verification and rollback

| What | Today | Target |
|---|---|---|
| **Flasher (Pages)** | Every merge to main | Pages deployed by Actions **from a release tag only**. Verify: fetch the live `manifest.json` and images and compare with the release `SHA256SUMS`. Roll back by redeploying the previous tag. |
| **Proxy image `latest`** | Every main push touching `proxy/` | After CI passes, and (owner's call) only on release tags. Roll back by retagging the previous digest. |
| **Install script** | Clones floating `main` | Checks out the latest release tag by default. |
| **The owner's own panel (OTA)** | Manual | A script that: (1) records the version and `/api/config` without secrets; (2) keeps the previous OTA image; (3) holds a lock against automation; (4) uploads with Digest auth; (5) verifies the version, poll code 200, heap and largest block above their floors after a few minutes, and a non-black `/screen.bmp`; (6) re-flashes the previous image if verification fails. |
| **The owner's own proxy** | Manual | A compose deploy pinned to the new digest, with a health and function check (`/v1/health`, then one real `/v1/state` call). Rolls back to the previous digest on failure. |

**No deploy, release, tag, OTA upload or flash without the owner's OK.** Tell the owner in
advance which alerts the deploy will trigger (for example container health monitors for the
proxy).

---

## 5. CI runners

GitHub-hosted runners are the default. The self-hosted **`oob-ci`** runners (VMs on the
owner's home server) are an option, with these rules:

- **Select the runner from a repository variable**, with the hosted runner as the fallback.
  Pull requests always stay on hosted runners:
  ```yaml
  runs-on: ${{ github.event_name == 'pull_request' && 'ubuntu-24.04' || vars.CI_RUNNER || 'ubuntu-24.04' }}
  ```
- **Pull requests (fork code) never run on self-hosted machines.** This repo is public, so
  also:
  - never use `pull_request_target`;
  - set "require approval for all outside collaborators";
  - remember that `workflow_run` and scheduled jobs execute main's code only.
- **Releases are built on GitHub-hosted runners.**
- **Registration.** On a personal account, runners are registered **per repository**, so
  ScoreDeck must be registered separately before `CI_RUNNER` is set. The alternative is to
  move the repos to an organisation and share a runner group.
- **The minutes argument does not apply here.** Standard hosted runners are free for public
  repositories, so the reasons to use self-hosted for ScoreDeck are speed and cached toolchains
  (the ESP32 core, LVGL, the visual-baseline container). It stays optional.
- **Split long suites by measured time**, not by test count. The likely candidates are the
  visual baselines and the fuzzers.

---

## 6. Adoption plan

Effort is rough, for one builder plus independent reviews. Each phase lands on its own
branch, through sections 3 and 4.

### Phase 0 — prerequisites (owner, under an hour)

- Turn on **branch protection** for main: require the CI checks, require a PR, no force push.
- Merge the `design-audit-2` work that carries `lint_invariants.py` and `lint_baseline.txt`
  (it is already planned).
- Run the first **leak hunt** (3.3) and decide which findings to fix in place and which need
  a history rewrite.
- Pick the canonical **FQBN** (2.7).

### Phase 1 — gate what already exists (~2–3 days)

1. **Desktop job in CI.** Build the harness on Linux with LVGL 8.3.11 pinned and checked,
   and run `make lint` (the font lint, plus the invariants once merged) as a blocking gate.
   Also run `make SAN=1 lint shots screens` under ASan/UBSan.
2. **Firmware size budget** in the existing compile job, with deltas in the job summary;
   pin `arduino-cli`.
3. **Portal header drift check** (`embed-portal.mjs`, then `git diff --exit-code`).
4. **Proxy coverage gate** (85 % lines) plus `npm audit` and `osv-scanner`.
5. **actionlint and shellcheck** job.
6. **Repo-policy tests v1:**
   - SHA pins and `permissions:`;
   - FQBN and version consistency;
   - `SD_VERSION`, manifest, `SECURITY.md` checksums and the SRI hash;
   - the trademark rule (moved from inline shell);
   - private addresses and home paths, with a shrink-only baseline.
7. **Image checks on PRs:** build, Trivy, non-root and read-only smoke. Gate `image.yml` on a
   green CI run for the same SHA.
8. **Renovate** config with a 14-day minimum release age, exact Node version, runner images
   pinned to `ubuntu-24.04`.

### Phase 2 — firmware logic under test (~1 week)

- Extract the feed parsers out of `net/api.cpp` and the input validators out of `svc/web.cpp`
  into pure modules.
- Host test target with ASan/UBSan. Coverage at 80 % or more on `core/` and the extracted
  modules.
- Regression tests for the firmware-side August fixes.
- libFuzzer targets (short runs on PRs, nightly long runs).
- cppcheck and clang-tidy with shrink-only baselines.
- Render-budget assertions in the harness.

### Phase 3 — deterministic visuals and browser tests (~1 week)

- Pinned render container, frozen clock and virtual `millis()`. Pixel-exact scenario × screen
  baselines, regenerated only by workflow input.
- Playwright and axe for the portal (sanitised mock, real CSP) and for the flasher.
- The manifest and vendored-bundle tests.
- Proxy fixtures per score model; validation, caching, coalescing and SSRF tests; ZAP
  baseline against the container in fixture mode.
- Unraid template contract tests.

### Phase 4 — signed releases and safe deploys (~3–5 days)

- Tag-triggered release workflow: CI-built firmware images with checksums, cosign and
  provenance; signed image with SBOM; the "how to verify" doc test.
- Pages served from releases. `latest` (owner's call) and `install.sh` follow tags.
- OTA and proxy deploy scripts with verification and rollback. Soak rule.

### Ongoing

- The review loop (section 3) on every security-sensitive change.
- OTA signing, secure boot and rollback: an owner decision, recorded when made.
- The baselines only shrink.
