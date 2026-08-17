# ScoreDeck — Full Security Review

**Target**: `/Users/francoraso/Documents/Development/Claude/ScoreDeck` (github.com/MrRonco/ScoreDeck, public)
**Date**: 2026-08-16
**Method**: Static review only. No builds, no git mutations, no file modifications, no contact with the panel (10.0.20.161) or any proxy. Read/Grep/Glob plus non-mutating shell.
**Scope**: `proxy/src/*.ts`, `firmware/ScoreDeck/src/svc/web.cpp`, `firmware/ScoreDeck/src/net/api.cpp`, `portal/index.html` + generated `svc/portal.h`, `install.sh`, `.github/workflows/*`, `flasher/index.html`, `unraid/*`, `Dockerfile`, dependency posture.

> **Authorization reminder**: only scan or test systems you own or have written authorization to test. Everything below is static analysis of source you own; do not point any of the active tooling referenced here at hosts you do not control.

---

## Executive Summary

| Severity | Count |
|---|---|
| CRITICAL | 1 |
| HIGH | 4 |
| MEDIUM | 8 |
| LOW | 8 |
| Informational / verified-good | 9 |

**Overall risk rating: HIGH.**

The threat model is stated honestly in the code and docs, and several hard things are done right — DNS-rebinding is genuinely blocked, every proxy route is behind the bearer check, the firmware's JSON→struct copies are uniformly bounded, and no secrets are committed. The risk concentrates in three places:

1. **The panel ships with no portal password**, and the *only* thing standing between any LAN device and an arbitrary firmware flash is that password. This is documented (a `Serial.println` warning and a portal banner), but a documented default is still the default.
2. **TLS certificate validation is disabled on every HTTPS call the panel makes**, while the documented production path (`docs/DEPLOY.md` Path B) is a Cloudflare Worker reached over the internet. That converts an on-path attacker anywhere between the panel and Cloudflare into full control of panel content and a bearer-token theft.
3. **The portal's one `innerHTML` sink** takes strings from the proxy catalog, is cached in `localStorage`, and sits behind a CSP that explicitly permits `'unsafe-inline'` scripts — so the file-header comment claiming "everything user-visible goes in through textContent, never innerHTML" is factually wrong at exactly one line, and the CSP does not catch it.

Exploitability legend used throughout: **[INTERNET]** = reachable/triggerable by an unauthenticated remote party, **[LAN]** = requires a device on the owner's local network, **[TOKEN]** = requires the shared bearer token, **[ON-PATH]** = requires MITM position on the panel↔proxy path.

---

# CRITICAL

## C1. Unauthenticated OTA firmware flash — full device takeover by any LAN host (default configuration)

- **OWASP**: A07:2025 Identification & Authentication Failures; A01:2025 Broken Access Control
- **CWE**: CWE-306 Missing Authentication for Critical Function, CWE-1188 Insecure Default, CWE-494 Download of Code Without Integrity Check
- **Exploitable from**: **[LAN]**
- **Location**:
  - `firmware/ScoreDeck/src/svc/web.cpp:75-85` — `guard()`
  - `firmware/ScoreDeck/src/svc/web.cpp:574-606` — `pageUpload()`
  - `firmware/ScoreDeck/src/svc/web.cpp:630` — `s_srv.on("/update", HTTP_POST, pageUpdate, pageUpload)`
  - `firmware/ScoreDeck/src/svc/web.cpp:637-638` — the acknowledgement

### Description

`guard()` is three tests, and the only one that authenticates anything is conditional:

```cpp
// web.cpp:75-85
static bool guard() {
  if (!hostAllowed())   { s_srv.send(403, "text/plain", "bad host"); return false; }
  if (!originAllowed()) { s_srv.send(403, "text/plain", "bad origin"); return false; }
  if (g_set.panelPass.length()) {          // ← the whole of "authentication"
    if (!s_srv.authenticate("admin", g_set.panelPass.c_str())) { ... }
  }
  return true;
}
```

`hostAllowed()` and `originAllowed()` are anti-CSRF / anti-rebinding controls, not authentication — they are trivially satisfied by any non-browser client (`curl` sends a correct `Host` and no `Origin`, and `originAllowed()` returns `true` on an absent `Origin`, `web.cpp:63`). `g_set.panelPass` defaults to empty (`core/state.cpp:102`, `p.getString(K_PPASS, "")`), and nothing in the first-boot flow requires setting it. The OTA upload handler applies exactly the same conditional (`web.cpp:578-579`), and `Update.begin(UPDATE_SIZE_UNKNOWN)` at `web.cpp:590` performs no signature verification — there is no secure boot, no image signing, no rollback anchor. The only integrity check on the image is a first-byte `0xE9` magic test, and that lives in the *browser* (`portal/index.html:624-626`), not on the device.

The firmware knows this. `web.cpp:637-638`:

```cpp
if (!g_set.panelPass.length())
  Serial.println("[web] WARNING: no portal password — anyone on this LAN can reflash this panel");
```

A serial-console warning is invisible to a user who flashed via `flasher/index.html` and never opened a terminal. The portal banner (`portal/index.html:430-435`) is only seen if the user opens the portal at all — the panel is fully usable via its touch UI without ever doing so. `flasher/index.html:74-75` even instructs users to update with an unauthenticated `curl -F "update=@scoredeck-ota.bin" http://scoredeck.local/update`, normalising the no-password path.

### Impact

Persistent arbitrary code execution on the ESP32-S3, from any host on the same L2 segment, in a single unauthenticated request. Post-flash the attacker owns a device that holds the Wi-Fi PSK (`K_PASS`, NVS) and the proxy bearer token (`K_TOKEN`) in NVS, sits inside the network permanently, and has a camera-less but network-capable foothold. Reverting requires physical USB access.

Even without flashing, the same guard fronts `/api/config` GET, which returns the proxy bearer token in plaintext (`web.cpp:264`) and the Wi-Fi SSID (`web.cpp:232`), and `/api/wifi`, `/api/reset`, `/api/forget`.

### Exploitation scenario

A guest phone, a compromised IoT bulb, or a laptop with a malicious npm postinstall on the same VLAN as the panel:

```
# 1. Confirm the panel and read its secrets — no credentials needed
curl -s http://scoredeck.local/api/config      # -> {"ssid":"...","token":"<proxy bearer>",...}

# 2. Flash attacker firmware — no credentials needed
curl -F "update=@evil.bin" http://scoredeck.local/update
```

Note the panel's own IoT-VLAN placement (documented in `unraid/scoredeck-boot.sh:4-5` as VLAN 20, isolated from the trusted LAN) *reduces* blast radius but does not remove it: everything else on that VLAN can reach the panel, and the panel holds the credentials to reach the proxy.

### Remediation

1. **Require a portal password before `/update`, `/api/wifi`, `/api/reset`, `/api/forget` and `/api/config` GET are reachable.** Make `guard()` fail closed when `panelPass` is empty for those routes — refuse the operation and tell the user to set a password on the panel. The touch UI already has a settings pane; the password can be set there.
2. Alternatively, generate a random per-device password at first boot, display it on the panel's screen (it has a 7-inch display — this is nearly free), and store it. That preserves "no configuration required" while removing the null default.
3. Enable **ESP32 Secure Boot v2 + signed OTA** (`esp_ota_ops` supports `esp_secure_boot` verification), so even an authenticated flash must carry a signature you control. This is the durable fix; item 1 is the urgent one.
4. Add a rate limit / lockout on `/update` regardless.

---

# HIGH

## H1. Stored XSS in the portal via the proxy catalog → `innerHTML`, not mitigated by the CSP

- **OWASP**: A05:2025 Injection (XSS)
- **CWE**: CWE-79 Improper Neutralization of Input During Web Page Generation, CWE-1021
- **Exploitable from**: **[LAN]** (and **[ON-PATH]** when the proxy is `http://`)
- **Location**:
  - `portal/index.html:341-343` — the sink
  - `firmware/ScoreDeck/src/svc/portal.h:355` — same code, as shipped in firmware flash
  - `portal/index.html:291-292` — the (incorrect) claim that this cannot happen
  - `portal/index.html:502-504, 521` — `localStorage` persistence
  - `firmware/ScoreDeck/src/svc/web.cpp:205-207` — the CSP that does not stop it

### Description

The portal is scrupulous about `textContent` everywhere — `el()` at `portal/index.html:285-286` sets `textContent`, and the board, favourites, search results, diagnostics and toasts all go through it. There is exactly one exception:

```js
// portal/index.html:341-343
row.innerHTML = '<div class="k"><input type="checkbox" data-slug="' + l.slug +
  '"' + (on.has(l.slug) ? ' checked' : '') + '> ' + l.label +
  ' <small>' + l.slug + '</small></div>';
```

`l.slug` and `l.label` come straight from `CAT.leagues`, and `CAT` is whatever the `/v1/catalog` response contained (`portal/index.html:507-520`). `l.slug` is interpolated *inside an unquoted-terminating attribute* and `l.label` in element content — both trivially escapable. The comment 50 lines above says:

```js
/* Upstream strings reach this page. Everything user-visible goes in through
   textContent, never innerHTML — the CSP is a backstop, not the plan. */
```

That is accurate for every line except this one.

**The CSP does not backstop it.** `web.cpp:205-207` sets `script-src 'unsafe-inline'`, which by definition permits inline event handlers. A payload of `"><img src=x onerror=fetch('/api/config').then(...)>` executes.

**It is persistent.** `loadCatalog()` writes the catalog to `localStorage['sd.cat']` (`portal/index.html:521`) and on every subsequent visit loads from there and calls `renderLeagues()` before any network fetch (`portal/index.html:502-504`). One hostile catalog response poisons the owner's browser for that origin indefinitely, surviving proxy repair.

### Impact

Script execution in the origin of the panel's portal (`http://scoredeck.local`). From there:
- `GET /api/config` → proxy bearer token (`web.cpp:264`) and Wi-Fi SSID, exfiltrated.
- `POST /update` with an attacker binary → firmware flash **even when a portal password is set**, because the victim's browser holds/replays the Basic credentials and the request is same-origin so `Origin`/`Host` checks pass. This is the path that defeats the C1 mitigation.
- `POST /api/config` to repoint the proxy, `POST /api/reset` to factory-wipe.

### Exploitation scenario

Two realistic deliveries, and the second works even against a hardened panel:

**(a) LAN attacker, no panel password** — attacker POSTs `/api/config` setting `proxy` to `http://attacker/`; the owner later opens the portal's Teams tab and clicks "Load catalog"; the attacker's `/v1/catalog?teams=1` returns `{"leagues":[{"slug":"a\"><img src=x onerror=/*payload*/>","label":"NHL","family":"hockey"}], "t":[]}`.

**(b) On-path attacker, panel password set** — the documented LAN deployment is a plaintext `http://192.168.x.x:8787` proxy (`portal/index.html:206` placeholder, `docs/DEPLOY.md`). The portal fetches the catalog **from the browser, directly to that plaintext URL** (`portal/index.html:507-510`). An ARP-spoofing or rogue-AP attacker on the same Wi-Fi rewrites that response, gets script execution in the portal origin, and rides the owner's already-authenticated session straight to `/update`. The panel password buys nothing here.

### Remediation

Replace the `innerHTML` construction with DOM building, consistent with the rest of the file:

```js
const wrap = el('div', 'k');
const cb = document.createElement('input');
cb.type = 'checkbox'; cb.dataset.slug = l.slug; cb.checked = on.has(l.slug);
wrap.append(cb, document.createTextNode(' ' + l.label + ' '), el('small', '', l.slug));
row.append(wrap);
```

Additionally: (1) validate `l.slug` against `/^[a-z0-9.]{2,8}$/` and `l.label` against a length+charset rule on arrival, since the device's own `apiLeaguesPost` (`web.cpp:357`) already enforces exactly that shape; (2) drop `'unsafe-inline'` from `script-src` by moving the portal script to an external `/portal.js` served from PROGMEM, or by hashing the single inline block (`script-src 'sha256-…'`) — `tools/embed-portal.mjs` can compute the hash at generate time; (3) version the `localStorage` cache key and validate on read. Regenerate `svc/portal.h` after any change.

---

## H2. TLS certificate validation disabled on every HTTPS request the panel makes

- **OWASP**: A04:2025 Cryptographic Failures
- **CWE**: CWE-295 Improper Certificate Validation
- **Exploitable from**: **[ON-PATH]** — including anywhere on the public internet path for the documented Cloudflare deployment
- **Location**: `firmware/ScoreDeck/src/net/api.cpp:95, 278, 362, 455, 601, 659, 702` (`secure.setInsecure()`); `api.cpp:845, 874` (deprecated single-argument `http.begin(url)`)

### Description

Every HTTPS code path disables verification:

```cpp
// api.cpp:94-95 (pollOnce) — and identically at 277-278, 361-362, 452-455, 600-601, 658-659, 701-702
const bool https = strncmp(job.url, "https:", 6) == 0;
if (https) secure.setInsecure();   // TODO(P1): pin the proxy cert
```

The `TODO(P1)` is honest but unshipped. `netProbeProxy()` (`api.cpp:845`) and `netRelayGet()` (`api.cpp:874`) use the deprecated `http.begin(url)` overload with no client object, which likewise performs no CA validation for `https://`.

The bearer token is attached to every one of these requests (`api.cpp:108, 285, 369, 460, 603, 666, 708, 847, 876`).

`docs/DEPLOY.md:244-247` calls the Cloudflare Workers path "what the project documents as the production path", and it is an `https://scoredeck.<sub>.workers.dev` URL traversing the public internet.

### Impact

Any on-path attacker — hostile Wi-Fi, a compromised home router, a malicious upstream, DNS hijack of `workers.dev` resolution — can:
1. **Steal the shared bearer token** on the first poll (it is a static secret sent every 12–900 s, forever).
2. **Serve arbitrary JSON to the panel.** That is not cosmetic: the response drives `settimeofday()` (`api.cpp:234-238`), the poll cadence (`api.cpp:229`), and — combined with H1 — is the exact input that reaches the portal's `innerHTML` sink via `/api/relay`.
3. **Trigger M2** (unbounded response bodies) to crash-loop the device.

### Remediation

Pin the proxy's certificate or CA. `WiFiClientSecure` supports `setCACert()` / `setCACertBundle()`; for `workers.dev` the ISRG/Google Trust Services roots are stable enough to pin, or embed the Cloudflare leaf's SPKI. Provide an explicit, per-device opt-out (`"I use a self-signed LAN proxy"`) rather than defaulting to insecure. Convert `netProbeProxy`/`netRelayGet` to the `begin(client, url)` form so they share the same policy. If pinning is genuinely infeasible on-device, at minimum refuse `https://` rather than silently downgrading the guarantee it implies — a plaintext `http://` LAN proxy is a more honest posture than an `https://` URL with no verification.

---

## H3. Flasher page loads `esp-web-tools` from an unpinned CDN with no integrity check

- **OWASP**: A03:2025 Software Supply Chain Failures; A08:2025 Software & Data Integrity Failures
- **CWE**: CWE-829 Inclusion of Functionality from Untrusted Control Sphere, CWE-1104 Use of Unmaintained/Unverified Third-Party Components
- **Exploitable from**: **[INTERNET]**
- **Location**: `flasher/index.html:8`

```html
<script type="module" src="https://unpkg.com/esp-web-tools@10/dist/web/install-button.js?module"></script>
```

### Description

Three compounding problems in one line: the version is a **floating major range** (`@10` resolves to whatever the newest 10.x is at page load), there is **no Subresource Integrity** attribute, and the module it loads is the code that **drives Web Serial to write flash on the user's hardware**. `flasher/manifest.json` and the `.bin` images are served same-origin, but the component that reads and writes them is not.

This is the highest-privilege third-party import in the repository. Every other risk in this review is bounded by "an attacker who already reached your LAN"; this one is bounded by "an attacker who reached npm, unpkg, or the `esp-web-tools` maintainer account", and it lands directly on the machines of everyone who ever installs ScoreDeck.

### Impact

A compromised `esp-web-tools` 10.x publish, a compromised unpkg edge, or a maintainer-account takeover results in attacker-chosen firmware being written to every ScoreDeck panel flashed from the project's own install page — with the user's explicit "yes, flash it" click supplying the Web Serial permission. Secondary impact: the same script runs in the page origin and can read anything there.

### Exploitation scenario

Attacker publishes `esp-web-tools@10.99.0` after taking over the package (the 2024–2026 npm account-takeover pattern: `event-stream`, `ua-parser-js`, `rspack`, `@solana/web3.js`). Within minutes every visitor to the GitHub-Pages-hosted `flasher/index.html` fetches it — no repo change, no release, no notification. The malicious `install-button.js` presents an identical UI and flashes a trojaned image alongside or instead of the legitimate one.

### Remediation

1. **Vendor the file.** `esp-web-tools` is a static asset; commit `flasher/vendor/esp-web-tools-10.x.y/install-button.js` and serve it same-origin. This removes the CDN entirely and is the recommendation.
2. If a CDN is required: pin the exact version (`esp-web-tools@10.3.2`, not `@10`) **and** add `integrity="sha384-…" crossorigin="anonymous"`. Modern Chrome/Edge — the only browsers this page supports — honour `integrity` on `<script type="module">`.
3. Add a CSP to the flasher page: `script-src 'self'` (after vendoring), `connect-src 'self'`.
4. Publish SHA-256 checksums for `scoredeck-merged.bin` / `scoredeck-ota.bin` in the README so users can verify what they flashed.

---

## H4. Proxy authentication fails open when `SD_TOKEN` is unset

- **OWASP**: A02:2025 Security Misconfiguration; A10:2025 Mishandling of Exceptional Conditions
- **CWE**: CWE-1188 Insecure Default Initialization, CWE-306, CWE-636 Not Failing Securely
- **Exploitable from**: **[INTERNET]** on the documented Workers path; **[LAN]** on the self-host path
- **Location**:
  - `proxy/src/app.ts:95-98` — `if (env.token) { ...check... }`
  - `proxy/src/server.ts:24` — `token: process.env.SD_TOKEN` (may be `undefined`)
  - `proxy/src/worker.ts:56` — `token: env.SD_TOKEN` (may be `undefined`)
  - `unraid/scoredeck-proxy.xml:55` — `Default=""`, description: *"Leaving this empty runs the proxy open to your network."*

### Description

```ts
// app.ts:95-98
if (env.token) {
  const auth = c.req.header('authorization');
  if (auth !== `Bearer ${env.token}`) return c.json({ error: 'unauthorized' }, 401);
}
```

No token configured means no authentication on any route. There is no startup abort, no loud log, and the only runtime signal is `server.ts:37` printing `token=none` among other text. The `docker-compose.yml` self-host path is safe (`SD_TOKEN: "${SD_TOKEN:?set SD_TOKEN in .env}"`, `docker-compose.yml:12`, which hard-fails), and `install.sh:49-52` always generates one — but the two paths that bypass compose do not:

- **Cloudflare Workers** (`docs/DEPLOY.md:244-257`): `wrangler deploy` succeeds whether or not `wrangler secret put SD_TOKEN` was run. The result is a **publicly resolvable `*.workers.dev` URL with zero authentication**. The `scheduled()` handler (`worker.ts:65-87`) likewise builds `auth` as `undefined` and proceeds.
- **Unraid template**: `SD_TOKEN` has an empty default and Unraid will start the container with it blank.
- **Bare `docker run`** of the published `ghcr.io/mrronco/scoredeck-proxy:latest` image without `-e SD_TOKEN`.

### Impact

On the Workers path this is an internet-exposed, unauthenticated service that: proxies and caches ~25 ESPN league feeds on demand (an open scraping relay attributed to the owner's Cloudflare account, with the owner absorbing the CPU/subrequest bill and any ESPN rate-limit or ToS consequence); exposes all cached state; and is the amplification lever for **M3** and **M4**. On the self-host path it is LAN-wide unauthenticated access.

### Exploitation scenario

Owner follows `docs/DEPLOY.md` Path B, runs `npx wrangler deploy` first, intends to add the secret afterwards, gets distracted. `scoredeck.<sub>.workers.dev` is now indexed by internet-wide scanners (`workers.dev` subdomains are enumerable via Certificate Transparency). Anyone hits `/v1/catalog?teams=1` in a loop; each miss triggers ~20 upstream ESPN fetches with `limit=1000` (`espn.ts:282`).

### Remediation

Fail closed. In `worker.ts` and `server.ts`, refuse to serve when the token is absent unless an explicit `SD_ALLOW_ANONYMOUS=1` opt-out is set:

```ts
if (!env.SD_TOKEN && env.SD_ALLOW_ANONYMOUS !== '1') {
  return new Response(JSON.stringify({ error: 'SD_TOKEN is not configured' }), { status: 503 });
}
```

Also: make the token comparison constant-time (see L4), mark `SD_TOKEN` `Required="true"` **with validation** in the Unraid template, and add a `/v1/health` field reporting whether auth is enabled so the operator can verify.

---

# MEDIUM

## M1. No dependency lockfile is published — every self-host build floats

- **OWASP**: A03:2025 Software Supply Chain Failures
- **CWE**: CWE-1104, CWE-829
- **Exploitable from**: **[INTERNET]** (registry-side)
- **Location**: `.gitignore:28` (`proxy/package-lock.json`), `proxy/Dockerfile:6-7`, `.github/workflows/ci.yml:19`

`proxy/package-lock.json` exists on disk (38 packages, resolving `hono` 4.13.1, `typescript` 5.9.3, `sharp` 0.35.3) but is **gitignored and not tracked** — confirmed via `git ls-files`. The Dockerfile therefore copies only `package.json` and runs a floating install:

```dockerfile
# Dockerfile:6-7
COPY package.json ./
RUN npm install --omit=dev && npm cache clean --force
```

`install.sh:57` (`docker compose up -d --build`) rebuilds this image on **every install and every update**, resolving `hono: ^4.6.14` fresh each time. CI does the same (`ci.yml:19`).

### Impact

No reproducibility and no pin. A compromised `hono` release, or a compromised transitive dependency, is pulled into every self-hosted ScoreDeck proxy on the next `./install.sh` — with no diff visible in the repo, and `npm install` running lifecycle scripts. This is precisely the A03 scenario the 2025 Top 10 promoted to #3.

### Exploitation scenario

Attacker compromises a `hono` maintainer account and publishes `4.13.2` with a postinstall that reads `/proc/self/environ` (which contains `SD_TOKEN`) and beacons out. Every ScoreDeck user who runs `./install.sh` to update in the following hours builds and runs it, as root during build, inside their NAS.

### Remediation

Un-ignore and commit `proxy/package-lock.json`; change the Dockerfile to `COPY package.json package-lock.json ./` + `RUN npm ci --omit=dev`; change CI to `npm ci`. Add `npm audit signatures` to CI. Consider Dependabot/Renovate for controlled bumps. (Note: the *currently resolved* `hono` 4.13.1 is above all 2026 advisories, and this codebase uses none of the affected modules — `hono/jsx`, `hono/cors`, `serve-static`, `@hono/node-server` — so there is no live CVE exposure today. The finding is about the absence of a pin, not a present vulnerability.)

## M2. Six of seven firmware HTTP fetches have no response-size limit, and the seventh's limit is dead code

- **OWASP**: A10:2025 Mishandling of Exceptional Conditions
- **CWE**: CWE-770 Allocation of Resources Without Limits, CWE-400
- **Exploitable from**: **[ON-PATH]** (via H2) or a compromised/misconfigured proxy
- **Location**: `api.cpp:121-127` (the ineffective guard); `api.cpp:287, 371, 462, 612, 668, 710` (no guard at all)

Only `pollOnce()` attempts a size check:

```cpp
// api.cpp:121-127
const int len = http.getSize();
if (len > 48 * 1024) { ... return false; }
const String body = http.getString();
```

`HTTPClient::getSize()` returns the `Content-Length` value, or **-1 when the response is chunked**. The file's own comments state that chunked is the normal case for both backends — `api.cpp:117-120` ("Both the Node dev server and Cloudflare send chunked") and `api.cpp:606-611`. `-1 > 49152` is false, so **the guard never fires against either real deployment**; `getString()` then reads the entire body into a heap `String` with no bound.

The other six fetches — `gameOnce` (287), `standingsOnce` (371), `getJson` (462, used by lineup and player), `catalogOnce` (612), `newsOnce` (668), `storyOnce` (710) — have no check at all. `standingsOnce:375-376` and `newsOnce:672-673` additionally deserialize **without a filter** into 24 KB and 16 KB documents.

### Impact

The panel has ~17–40 KB of free internal heap by the project's own account (`unraid/scoredeck-proxy.xml` overview) and needs ~16.4 KB contiguous for mbedTLS (`config.h:79-81`). A multi-megabyte response exhausts the heap inside `getString()`. Best case the allocation fails and the poll returns false; realistic case is heap fragmentation that starves the TLS gate and the LVGL display buffers, producing a panel that reboots in a loop or renders corrupt — a persistent DoS that survives power cycles as long as the hostile response persists.

### Exploitation scenario

An on-path attacker (H2 gives this for free on the HTTPS path; ARP spoofing gives it on the LAN HTTP path) responds to `GET /v1/standings/nhl` with `Transfer-Encoding: chunked` and 8 MB of `{"cols":[` padding. The panel wedges. Repeat on every reconnect.

### Remediation

Do not rely on `getSize()`. Stream with a hard byte budget, or check `getSize()` **and** treat `-1` as "unknown → enforce a streaming cap":

```cpp
const int len = http.getSize();
if (len > MAX_BODY) { ...; return false; }
// len == -1 (chunked): read with an explicit cap
String body;
if (!readCapped(http.getStream(), body, MAX_BODY)) { ...; return false; }
```

Apply a per-endpoint `MAX_BODY` to all seven call sites (the natural values are already known: 48 KB state, ~1.3 KB catalog, ~6.2 KB story). Add ArduinoJson filters to `standingsOnce` and `newsOnce` for consistency with the other paths.

## M3. Unbounded cache-key growth in `MemoryStore` — memory exhaustion and upstream amplification

- **OWASP**: A04 (API4:2023 Unrestricted Resource Consumption); A10:2025
- **CWE**: CWE-770, CWE-400
- **Exploitable from**: **[TOKEN]**, or **[INTERNET]** unauthenticated when H4 applies
- **Location**: `proxy/src/store.ts:12-28`; `proxy/src/app.ts:403-404` (`grp`); `app.ts:288-296` (`story:${id}`); `app.ts:311, 330, 349` (per-id keys)

`MemoryStore` is a bare `Map` with no size cap and no eviction — expired entries are deleted only if that exact key is read again (`store.ts:17-21`). Two routes let a caller mint unlimited distinct keys:

```ts
// app.ts:403-404
const grp = Number.parseInt(c.req.query('grp') ?? '0', 10) || 0;
const key = `stand:${lg.slug}:${grp}`;
```

`normalizeStandings` clamps the index to the available groups (`espn.ts:253`), so every value of `grp` returns the *same* table — but each one gets its own cache entry **and** triggers its own upstream ESPN fetch on the miss (`espn.ts:303-309`). `/v1/story/:id` accepts `\d{1,12}` (10¹² keys, 3600 s TTL each, `app.ts:288, 296`); `/v1/game/:league/:id`, `/v1/lineup/...`, `/v1/player/...` are the same shape.

### Impact

Heap exhaustion of the Node container (self-host path — the container has no memory limit in `docker-compose.yml`), and simultaneous request amplification against ESPN attributed to the owner's IP. On Workers the Cache API absorbs the storage but the subrequest amplification remains.

### Exploitation scenario

`for i in $(seq 1 200000); do curl -H "Authorization: Bearer $T" "http://proxy:8787/v1/standings/nhl?grp=$i" & done` — 200 k identical standings tables retained for 600 s each, plus 200 k `site.api.espn.com` requests in a burst. On a Pi or a NAS the container OOMs; ESPN rate-limits or blocks the source IP, breaking the appliance for its legitimate user.

### Remediation

1. Clamp `grp` at the boundary: `const grp = Math.min(Math.max(0, parseInt(...) || 0), 16);` — the real group count is single digits.
2. Give `MemoryStore` a bounded LRU (cap entries and total bytes) with periodic sweep, not read-triggered expiry. ~200 lines of `Map` + insertion-order eviction, or a small dependency.
3. Add per-token/per-IP rate limiting — there is currently none anywhere in `proxy/src` (verified by grep).
4. Add a memory limit (`mem_limit`) to `docker-compose.yml` so exhaustion degrades to a container restart rather than host pressure.

## M4. No timeout and no size cap on any upstream ESPN fetch

- **OWASP**: A10:2025; A04 (API4)
- **CWE**: CWE-1088 Synchronous Access Without Timeout, CWE-400
- **Exploitable from**: **[TOKEN]** to trigger; the failure mode is upstream-induced
- **Location**: `proxy/src/espn.ts:219-224, 282-286, 304-309, 431-436, 442-447, 459-462, 606-612`

Every upstream call is a bare `fetchImpl(url, { headers })` with **no `AbortSignal`** (verified: zero matches for `AbortSignal|signal:|timeout` in `proxy/src`) and no cap on `res.json()`. The golf scoreboard is 1.2 MB and tennis 658 KB by the project's own measurements.

`/v1/catalog?teams=1` is the sharpest case: on a cache miss it loops ~22 leagues **sequentially** (`app.ts:129-141`), each a `?limit=1000` fetch, each unbounded in time and size, with no lock against a thundering herd. Two concurrent cold `/v1/catalog?teams=1` requests do all of that work twice.

### Impact

Self-host (Node): a slow or hung ESPN endpoint holds request handlers open indefinitely with the full payload buffered per in-flight request; concurrent requests multiply resident memory by the number of clients. Combined with M3, memory exhaustion. Workers: the 10 ms CPU budget and platform subrequest timeouts contain it, but `docs/DEPLOY.md:279-289` already flags golf/tennis as unmeasured there.

### Exploitation scenario

Twenty concurrent `GET /v1/catalog?teams=1` immediately after a restart (cache cold) → ~440 concurrent unbounded ESPN fetches from one container, each buffering to completion. The proxy becomes unresponsive to the panel; the panel shows `stale`.

### Remediation

Add `AbortSignal.timeout(8000)` to every `fetchImpl` call in `espn.ts`. Cap response size by reading through the body stream with a byte budget (1.5 MB is generous given the known payloads) rather than `res.json()` directly. Add a single-flight lock around the `catalog:teams:v1` build so concurrent cold requests share one computation.

## M5. Cross-origin GET side effects — drive-by display DoS from any internet page

- **OWASP**: A01:2025 Broken Access Control (CSRF)
- **CWE**: CWE-352 Cross-Site Request Forgery, CWE-400
- **Exploitable from**: **[INTERNET]** (requires the owner to visit a page; no LAN foothold)
- **Location**: `web.cpp:61-73` (`originAllowed`), `web.cpp:519-555` (`pageScreen`), `web.cpp:629` (route), `web.cpp:442-447` (`apiProbe`)

`originAllowed()` returns `true` when `Origin` is absent:

```cpp
// web.cpp:61-65
static bool originAllowed() {
  String o = s_srv.header("Origin");
  if (!o.length()) return true;              // non-browser client
```

For POST this is sound — browsers send `Origin` on cross-origin POSTs, including form posts. But **browsers omit `Origin` on no-CORS GET subresource loads** (`<img>`, `<iframe>`, `<link>`, `<script>`), and `Host` will be `scoredeck.local`, which `hostAllowed()` accepts (`web.cpp:56`). So every GET route is triggerable cross-origin, side effects included.

`/screen.bmp` is the sharp one. Its own docstring:

> *"MANUAL ONLY, and never polled: reading back the framebuffer blocks the display for roughly two seconds."* — `web.cpp:516-517`

Nothing enforces "manual only". `/api/probe` (`web.cpp:442-447`) similarly forces a device-side outbound HTTP request with a 4 s blocking timeout in loop context (`api.cpp:836-853`).

### Impact

An attacker-controlled or ad-injected page that the owner opens on any device on the same network — phone, laptop — can hold the panel's display frozen indefinitely. The response bodies are opaque to the attacker (no CORS headers, canvas tainting), so this is availability, not confidentiality. But it is a **remote, no-foothold** availability attack on a physical appliance, and mDNS resolution of `scoredeck.local` works from an arbitrary origin on macOS, Linux and Windows 10+.

### Exploitation scenario

```html
<script>
setInterval(() => { new Image().src = 'http://scoredeck.local/screen.bmp?' + Math.random(); }, 500);
</script>
```
Each hit is a 1.15 MB framebuffer readback that stalls LVGL ~2 s (`web.cpp:513, 543-553`). The panel is unusable while the tab is open. Note Chrome's Private Network Access protections are not fully enforced for subresource loads at time of writing, so this is not reliably blocked by the browser.

### Remediation

1. Require `Sec-Fetch-Site: same-origin` (or absent, for non-browsers) on `/screen.bmp` and `/api/probe`. Add `"Sec-Fetch-Site"` and `"Sec-Fetch-Mode"` to `collectHeaders` at `web.cpp:613-614` and reject `cross-site`. This is the modern, correct control and it costs two lines.
2. Rate-limit `/screen.bmp` on the device (one capture per N seconds) and `/api/probe` similarly.
3. Consider requiring the portal password for `/screen.bmp` unconditionally — it is a diagnostic.

## M6. Panel password uses HTTP Basic over plaintext, with no rate limiting or lockout

- **OWASP**: A07:2025 Identification & Authentication Failures; A04:2025
- **CWE**: CWE-319 Cleartext Transmission of Sensitive Information, CWE-307 Improper Restriction of Excessive Authentication Attempts
- **Exploitable from**: **[LAN]**
- **Location**: `web.cpp:79-80` (`authenticate` / `requestAuthentication`), `web.cpp:579` (same in the OTA path), `web.cpp:38` (`WebServer s_srv(80)` — HTTP only)

```cpp
// web.cpp:78-82
if (g_set.panelPass.length()) {
  if (!s_srv.authenticate("admin", g_set.panelPass.c_str())) {
    s_srv.requestAuthentication();
```

`WebServer::requestAuthentication()` defaults to `BASIC_AUTH`, so the credential travels base64-encoded over port 80. There is no attempt counter, no backoff, no lockout, and no logging of failures — an attacker can grind the password at line rate. The username is the fixed literal `"admin"`, halving the search space. No minimum length or complexity is enforced on `ppass` anywhere in `apiConfigPost` (`web.cpp:301, 315`) — a one-character password is accepted.

### Impact

The single control that mitigates C1 is transmitted in the clear on a network that, by the project's own design, hosts untrusted IoT devices, and can be brute-forced without limit. Anyone passively sniffing the Wi-Fi (or on a hub/mirrored port) recovers it from one legitimate portal visit.

### Exploitation scenario

Attacker on the IoT VLAN runs `hydra -l admin -P rockyou.txt http-get://scoredeck.local/api/config` — no lockout, no delay beyond the ESP32's single-connection loop. Or waits for the owner to open the portal and reads the `Authorization: Basic` header off the wire. Either way, C1 becomes available even on a "hardened" panel.

### Remediation

1. Use `DIGEST_AUTH` (`requestAuthentication(DIGEST_AUTH, "ScoreDeck", ...)`) — supported by `WebServer` and it stops the password crossing the wire in recoverable form. This is the cheap immediate win.
2. Add a failure counter with exponential backoff, and a lockout after N failures with the state shown on the panel's screen.
3. Enforce a minimum password length (≥8) in `apiConfigPost` alongside the existing validation block (`web.cpp:279-301`).
4. Longer term: serve the portal over HTTPS with a self-signed cert the user accepts once, or bind the portal to a session cookie issued after one Digest challenge.

## M7. Response-header / CSP injection via the unvalidated proxy URL

- **OWASP**: A05:2025 Injection
- **CWE**: CWE-113 Improper Neutralization of CRLF Sequences in HTTP Headers, CWE-116
- **Exploitable from**: **[LAN]** (or via H1)
- **Location**: `web.cpp:205-209` (interpolation), `web.cpp:279-283` (the only validation), `web.cpp:126-148` (`jsonField`)

```cpp
// web.cpp:205-209
String csp = "default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; "
             "img-src 'self' data:; ... connect-src 'self'";
if (g_set.proxy.length()) { csp += " "; csp += g_set.proxy; }
s_srv.sendHeader("Content-Security-Policy", csp);
```

`g_set.proxy` is checked only for an `http://`/`https://` prefix and a 96-byte length (`web.cpp:280-282`). It is never checked for whitespace, semicolons, CR or LF. `jsonField` (`web.cpp:126-148`) extracts the value as the raw bytes between two `"` characters with no JSON unescaping and no control-character filtering — a body containing literal `\r\n` inside the quoted value carries those bytes straight into `g_set.proxy`, into NVS, and back out into `sendHeader()`.

Note the contrast: `jstr()` at `web.cpp:106-117` correctly drops control bytes on the *output* side for JSON. The header path has no equivalent.

### Impact

Two effects. **(a) CSP weakening** — no CRLF needed: `http://x" ; script-src * ; connect-src *` or simply `http://x https://evil.example` appends attacker-chosen sources to `connect-src`, letting injected script (H1) exfiltrate to an arbitrary host. **(b) Response splitting** — with raw CRLF, arbitrary headers or a forged second response on the panel's own origin.

### Exploitation scenario

`curl -X POST http://scoredeck.local/api/config --data-binary $'{"proxy":"http://a\r\nX-Injected: 1"}'` (no auth required by default, C1). Every subsequent load of `/` carries the injected header. Chained with H1, the CSP variant is what turns a same-origin XSS into a data-exfiltrating one.

### Remediation

Validate `proxy` strictly on write — parse it as a URL and reject anything containing characters outside `[A-Za-z0-9:/._~%-]`, in particular whitespace, `;`, `'`, CR and LF:

```cpp
for (size_t i = 0; i < sProxy.length(); i++) {
  const unsigned char ch = sProxy[i];
  if (ch < 0x21 || ch > 0x7E || ch == ';' || ch == '\'' || ch == '"')
    return fail(400, "proxy url contains an illegal character");
}
```

Separately, strip control bytes inside `jsonField()` itself so no field can ever carry them, and reduce `script-src` per H1's remediation.

## M8. GitHub Actions: floating action versions and no explicit `permissions`, with `packages: write` on the image publisher

- **OWASP**: A03:2025 Software Supply Chain Failures; A08:2025
- **CWE**: CWE-829, CWE-250 Execution with Unnecessary Privileges
- **Exploitable from**: **[INTERNET]** (action-supply-chain side)
- **Location**: `.github/workflows/ci.yml:15, 16, 27, 28, 52, 55, 63`; `.github/workflows/image.yml:11-13, 15-18, 23, 31`

Every action is referenced by a **mutable major tag** — `actions/checkout@v4`, `actions/setup-node@v4`, `arduino/setup-arduino-cli@v2`, `gitleaks/gitleaks-action@v2`, `docker/setup-qemu-action@v3`, `docker/setup-buildx-action@v3`, `docker/login-action@v3`, `docker/metadata-action@v5`, `docker/build-push-action@v6`. A tag is a pointer; whoever controls the action repo controls what runs.

`ci.yml` has **no `permissions:` block at all** (repository default applies, which is read/write on repos created before the default changed). `image.yml:11-13` correctly scopes to `contents: read` + `packages: write`, but that write permission is exactly what an attacker wants: `unraid/scoredeck-proxy.xml:5` and `proxy/docker-compose.yml:3` both point users at `ghcr.io/mrronco/scoredeck-proxy:latest`.

### Impact

A compromised action (the `tj-actions/changed-files` March 2025 incident is the canonical example — a retagged `v*` pointer exfiltrated secrets from tens of thousands of repos) running in `image.yml` inherits `packages: write` and can push a backdoored `:latest` image that every Unraid user pulls on their next update. In `ci.yml` it inherits whatever the repo default grants, potentially `contents: write` on `main` — which install.sh then distributes via `git pull --ff-only`.

### Remediation

1. **Pin every action to a full commit SHA** with the version in a trailing comment: `uses: actions/checkout@11bd71901bbe5b1630ceea73d27597364c9af683 # v4.2.2`.
2. Add `permissions: contents: read` at the top level of `ci.yml`, and keep the job-level grant in `image.yml` as-is.
3. Enable Dependabot for `github-actions` so pinned SHAs still get updated deliberately.
4. Sign published images with cosign and document verification, so a poisoned `:latest` is detectable.
5. Minor: `ci.yml:19` runs `npm install` on fork PR code. The `pull_request` trigger correctly gives a read-only token and no secrets, so this is acceptable — but switching to `npm ci` (per M1) also stops arbitrary version resolution in CI.

---

# LOW

## L1. `install.sh` writes `proxy/.env` with the default umask — token is world-readable

`install.sh:51`: `printf 'SD_TOKEN=%s\nTZ=%s\n' ... > .env`. No `umask 077` and no `chmod 600`. Under the default umask 022 the file lands at mode 0644 inside `$HOME/scoredeck/proxy` (itself 0755 from `git clone`). Any local user on the NAS, Pi or shared host reads the bearer token. **Fix**: `umask 077` before the write, or `chmod 600 .env` immediately after; `chmod 700 "$DIR"`.

## L2. `install.sh` passes the token as a command-line argument

`install.sh:60-67`: `TOKEN="$(grep '^SD_TOKEN=' .env | cut -d= -f2)"` then `curl -H "Authorization: Bearer $TOKEN" ...` inside a loop of up to 30 iterations. The full `curl` argv — including the token — is visible in `/proc/*/cmdline` to every local user for the duration. **Fix**: pass it via a file or stdin: `curl -H @<(printf 'Authorization: Bearer %s' "$TOKEN")`, or `curl --config -` with the header on stdin. Printing the token to the terminal at `install.sh:81` is intentional and acceptable — the user needs it — but consider noting that it is now in shell scrollback.

## L3. `/api/relay` allowlist admits percent-decoded CRLF and `..` inside its length budget

`web.cpp:459-462`:
```cpp
const bool ok = p == "/v1/catalog" || p == "/v1/catalog?teams=1" || p == "/v1/health" ||
                (p.startsWith("/v1/teams/") && p.length() <= 20);
```
`s_srv.arg("p")` returns the **percent-decoded** query value, so `p=/v1/teams/%0d%0aX:` passes the prefix and length tests carrying a raw CRLF, and `p=/v1/teams/../..` passes carrying traversal. That string is concatenated into a URL and handed to `HTTPClient::begin()` (`api.cpp:871-874`), which does not sanitise the request-line. The 10-byte budget after the prefix bounds the damage severely, and the target is the user's own trusted proxy — hence LOW, not higher. The design comment at `web.cpp:452-455` and `api.cpp:857-862` is right that a path allowlist is the correct shape; the allowlist just needs to be exact. **Fix**: require `p` to match `/^\/v1\/teams\/[a-z0-9.]{2,8}$/` rather than a prefix-plus-length test, and reject any byte `< 0x21` or `> 0x7E` in `p`.

## L4. Non-constant-time secret comparisons

`app.ts:97` (`auth !== \`Bearer ${env.token}\``) and the ESP32 `authenticate()` String compare both short-circuit on first mismatch. Remote timing extraction of a 48-hex-char token across a network is not practically achievable, hence LOW — but it is free to fix. **Fix**: `crypto.timingSafeEqual` over equal-length buffers in `app.ts`, after a length check.

## L5. Asset path prefix check lacks a separator boundary

`server.ts:15`: `if (!full.startsWith(ASSETS)) return undefined;` — `startsWith` on a path without a trailing separator would also accept `/data/assets-evil/...` for `ASSETS=/data/assets`. **Not reachable today**: `rel` is always `logos/<registry-slug>/<regex-validated-file>` or `players/<registry-slug>/<regex-validated-file>` (`app.ts:232-233, 355, 376-377`), so `join()` cannot escape. Defence-in-depth only. **Fix**: `full.startsWith(ASSETS + path.sep)`.

## L6. Bearer token traverses the LAN in cleartext on the documented self-host path

The device (`api.cpp:108` etc.) and the browser portal (`portal/index.html:507-510`) both send `Authorization: Bearer <token>` to whatever `CFG.proxy` is, and the documented LAN configuration is `http://192.168.x.x:8787` (`portal/index.html:206`, `docs/DEPLOY.md`). Passive Wi-Fi capture yields the token. This is an accepted trade-off for a LAN appliance and the token's blast radius is limited to the proxy — recorded for completeness. **Fix (optional)**: document that the token is LAN-cleartext, and that it should not be reused anywhere else.

## L7. Deprecated `HTTPClient::begin(url)` in two call sites

`api.cpp:845` (`netProbeProxy`) and `api.cpp:874` (`netRelayGet`) use the single-argument overload rather than `begin(client, url)`. Beyond the certificate concern already covered in H2, this bypasses the explicit `WiFiClient`/`WiFiClientSecure` selection every other fetch in the file performs, making TLS policy inconsistent across the codebase. **Fix**: convert both to the two-argument form sharing the same client construction as `pollOnce`.

## L8. Config-driven SSRF via `proxy` + `/api/probe` + `/api/relay`

An attacker who can POST `/api/config` (unauthenticated by default, C1) sets `proxy` to any `http://`/`https://` URL — only the scheme prefix and a 96-byte length are validated (`web.cpp:280-282`). `/api/probe` (`web.cpp:442-447` → `api.cpp:836-853`) then makes the panel issue a GET to that host and returns the status code and latency; `/api/relay` (`web.cpp:456-472` → `api.cpp:868-881`) returns the **response body** for the allowlisted `/v1/*` suffixes. That is a blind-plus-partial-read SSRF primitive originating from inside the IoT VLAN.

Rated LOW rather than MEDIUM because the prerequisite (`POST /api/config`) is the same access that already grants firmware flash (C1) — an attacker who has it does not need an SSRF. It becomes materially more interesting once C1 is fixed and a panel password is required, at which point the SSRF is authenticated. **Fix**: alongside the C1 and M7 fixes, reject `proxy` hosts that resolve to link-local/loopback/metadata ranges, or at minimum re-validate the configured host at relay time.

---

# Informational — verified good

These were specifically examined and found sound. Recording them so they are not re-litigated.

1. **No buffer overflow in `net/api.cpp`.** `copyStr` (`api.cpp:31-36`) is correct — `strncpy(dst, s, cap-1)` followed by an unconditional `dst[cap-1] = '\0'`, and it handles the null-`const char*` case. Every one of its ~60 call sites passes `sizeof` of the true destination. All bounded loops were checked against the declared array dimensions in `core/types.h`: `G.colCount ≤ LU_COLS` vs `P.vals[LU_COLS]` (`api.cpp:500-501`), `t.colCount ≤ ST_MAX_COLS` vs `row.cells[ST_MAX_COLS]` (`api.cpp:397-401`), `d.lsCount ≤ GD_LS_COLS` vs `lsA/lsH[GD_LS_COLS]` (`api.cpp:315-321`), and the `MAX_GAMES`/`MAX_LEAGUES`/`MAX_EVENTS`/`FLD_POOL`/`FLD_ROWS`/`LU_*`/`PC_STATS`/`NEWS_MAX` guards. `catalogOnce` (`api.cpp:628-631`) uses raw `strncpy` but terminates explicitly. **No missing bounds or truncation found on any copyStr-style write.** The truncation semantics are silent, which is the right choice for a display device.

2. **DNS rebinding is genuinely blocked.** `hostAllowed()` (`web.cpp:49-58`) validates `Host` against the names the device actually answers to (`scoredeck.local`, `scoredeck`, its own IP) *before* the Origin test — which is exactly the AirRadar rule quoted in the file header (`web.cpp:20-24`) and exactly the thing most ESP32 projects get wrong. A rebound `evil.com → 10.0.20.161` sends `Host: evil.com` and is rejected. This is correct and should not be weakened. (M5 is not a rebinding bypass; it is a separate absent-`Origin` GET issue.)

3. **CSRF on state-changing routes is correctly handled.** All mutating routes are `HTTP_POST` (`web.cpp:618-628`) and browsers send `Origin` on cross-origin POSTs including form posts, so `originAllowed()` (`web.cpp:61-73`) rejects them. The case-insensitive scheme+host compare (`web.cpp:66-72`) is the right call and the reasoning in the comment is correct.

4. **Every proxy route is authenticated.** `app.use('*', ...)` at `app.ts:82-101` runs before all handlers; there is no route-level exemption, and `/v1/health` is not special-cased. The only bypass is the `OPTIONS` preflight short-circuit at `app.ts:84-94`, which is limited to three read-only paths by `CORS_PATHS` (`app.ts:80`) and returns a 204 with no body. The `access-control-allow-origin: *` choice is safe here precisely because `credentials` are never allowed and the token must be supplied explicitly — the reasoning at `app.ts:69-79` is sound. Confirmed by tests at `proxy/test/app.test.ts:85-90, 145-150`.

5. **No SSRF or path traversal in the proxy's parameterised routes.** Upstream URLs are built from `LEAGUES[].path`, a fixed compile-time table (`registry.ts:8-46`) reached only through `league(slug)` map lookup — a caller cannot inject a path segment. `/v1/story/:id` requires `^\d{1,12}$` (`app.ts:288`); `/v1/game`, `/v1/lineup`, `/v1/player` the same (`app.ts:308, 329, 347`); `/v1/logo/:league/:file` requires `^[A-Za-z0-9]{1,5}@(48|96)\.bin$` (`app.ts:232`) and `/v1/head/:league/:file` requires `^\d{1,12}\.bin$` (`app.ts:376`). Ids are additionally `encodeURIComponent`'d at the fetch sites (`espn.ts:431, 607`). `parseFavs` (`app.ts:22-32`) caps at 20 entries and validates each id. Clean.

6. **Cache keys are not poisonable by unvalidated input.** `region` is `^[a-z]{2}$` or `'us'` (`app.ts:151, 309`), `date` derives from a validated IANA zone via `safeTz` (`app.ts:55-63`), league slugs come from the registry. The one exception is `grp`, covered as M3.

7. **Upstream string handling in the proxy is defensive.** `clamp()` (`espn.ts:27-31`) strips `[\x00-\x1f\x7f]` (verified by byte inspection, not by reading the source glyphs) and enforces a max length; every field taken from ESPN passes through it or through `num()`/`hex()`. `hex()` (`espn.ts:21-24`) requires exactly six hex digits. `normalizeStory` (`espn.ts:467-495`) strips tags and clamps to 6 KB on a word boundary.

8. **Secret handling is as documented, with one deliberate and disclosed exception.** `applySecret` (`web.cpp:191-196`) implements blank-keeps / `-`-clears correctly and is used for `token`, `panelPass` and the Wi-Fi PSK (`web.cpp:314-315, 481`). `/api/config` GET reports `hasToken`/`hasPass` as booleans (`web.cpp:236-237`) and **never** echoes the panel password or the Wi-Fi PSK. It *does* echo the proxy bearer token (`web.cpp:264`), and the comment at `web.cpp:261-263` states the rationale plainly. That rationale is sound *given* C1 — "anything that can read this endpoint can already flash arbitrary firmware" is true today. **It stops being true once C1 is fixed**, so when you gate `/update` behind a mandatory password, revisit this line: the portal needs the token for the direct-catalog fetch, but it could be delivered only over an authenticated session, or the direct fetch dropped in favour of `/api/relay`. Also note `apiConfigPost` validates everything before applying anything (`web.cpp:273-315`) — the partial-NVS-write lesson in the comment is correctly implemented, and `validFavs`/`apiLeaguesPost` reject rather than truncate.

9. **Repository hygiene is clean.** `git ls-files` (122 files) contains no `.env`, no `dev_defaults.h`, and no secret in `proxy/wrangler.toml` (verified: name, main, cron, observability only). `firmware/ScoreDeck/src/dev_defaults.h` exists untracked and holds only a LAN URL. `.gitignore:19-23` covers `.dev.vars`, `.env`, `.env.*`, `wrangler.toml.local`. CI runs a `gitleaks` job with full history (`ci.yml:48-57`) and an assets-provenance job (`ci.yml:59-72`). The Dockerfile runs as `USER node` (`Dockerfile:10`), installs no dev dependencies, needs no writable mount, and its healthcheck correctly supplies the bearer token. Third-party licensing is documented (`THIRD-PARTY-NOTICES.md`, `LICENSES/`). The one hygiene gap is the un-tracked lockfile (M1).

---

# Remediation priority

**Immediate (this week)**
1. **C1** — require a portal password (or a screen-displayed generated one) before `/update`, `/api/config` GET, `/api/wifi`, `/api/reset`, `/api/forget`. Single highest-value change in the repo.
2. **H1** — replace the one `innerHTML` at `portal/index.html:341`, regenerate `svc/portal.h`, and version the `localStorage` key.
3. **H3** — vendor `esp-web-tools` into `flasher/`, or pin the exact version with SRI.
4. **H4** — make the proxy refuse to serve without `SD_TOKEN` unless explicitly opted out.

**Short term (30 days)**
5. **H2** — pin the proxy certificate; remove `setInsecure()`.
6. **M2** — real body-size caps on all seven firmware fetches; stop trusting `getSize()` on chunked responses.
7. **M8** — SHA-pin all GitHub Actions; add `permissions: contents: read` to `ci.yml`.
8. **M1** — commit the lockfile; switch to `npm ci`.
9. **M5** — `Sec-Fetch-Site` check on `/screen.bmp` and `/api/probe`.
10. **M6** — Digest auth, failure backoff, minimum password length.
11. **M7** — strict character validation on the `proxy` field.

**Maintenance cycle**
12. **M3, M4** — bounded LRU store, `grp` clamp, fetch timeouts and size caps, single-flight on the catalog build, rate limiting.
13. **L1–L8** — `umask 077` in `install.sh`, token off the `curl` argv, exact-match relay allowlist, `timingSafeEqual`, path separator on the asset prefix check, two-argument `begin()`.
14. Long term: ESP32 Secure Boot v2 with signed OTA, and published SHA-256 checksums for the flasher images.
