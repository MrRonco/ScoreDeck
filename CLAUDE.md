# ScoreDeck

Live sports scoreboard on an ESP32-S3 7" panel: firmware (LVGL 8.3), a desktop SDL harness,
a TypeScript proxy, a browser portal served from flash, a web flasher, and a container image
with an Unraid template. Start with [`README.md`](README.md) and [`docs/`](docs/).

## Testing and review (required)

The full methodology, the current state and the adoption plan are in
[`docs/TESTING.md`](docs/TESTING.md). These rules are not optional:

- **Test first.** Write the test, watch it fail, then fix. Every bug fix starts with a test
  that reproduces it. For UI bugs that test is a scenario, a lint rule or a baseline.
- **CI gates block.** Never add `continue-on-error`, skip a test, or retry a flaky one: a
  flaky test is a bug to fix.
- **Baselines only shrink** (`desktop/lint_baseline.txt`, static analysis, policy tests).
  Visual baselines are regenerated only through the workflow input, and a person reviews them
  before they are committed.
- **No real world in CI or tests:** no live ESPN calls (recorded, trimmed fixtures only),
  no real panel, no flashing.
- **No personal data anywhere in the repo:** no real IPs, SSIDs, hostnames, home paths,
  tokens or e-mail addresses in code, fixtures, mock data or screenshots. Use the documented
  placeholders. Never commit logo or headshot binaries.
- **Supply chain:** actions are pinned by full SHA; tools and dependencies use exact
  versions, are at least 14 days old, and are verified by checksum or signature.
- **Generated files:** edit `portal/index.html`, then run `node tools/embed-portal.mjs`.
  Never edit `svc/portal.h` or `svc/portal_csp.h` by hand.
- **The builder never reviews its own work.** Changes to the proxy, `svc/web.cpp`, the portal,
  OTA, TLS, `install.sh`, the flasher or the workflows get a separate security review. Fix
  test-first, then the same reviewer re-checks, until no CRITICAL, HIGH or MEDIUM finding is
  open.
- **Leak hunt before publishing.** Check text, fixtures, image metadata and what every
  screenshot shows.
- **Integration:**
  1. CI is green on the branch tip.
  2. Merge with `--no-ff` and the message in a file.
  3. Verify main's CI by full commit SHA.
  4. Tag. Releases are built and signed by CI.
- **Merging to main is a deploy.** Pages serves the flasher from main, and the image's
  `latest` tag follows main.
- **No merge to main, tag, release, deploy, OTA upload or flash without the owner's OK.**
  Agents push their branch only.
