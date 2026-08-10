# The browser portal

`index.html` is the whole portal — one page, six hash views, no framework, no
build for the browser to do. It is served from the device out of PROGMEM and
hydrated from a small JSON API.

```bash
node portal/mock.mjs          # http://127.0.0.1:8080 with a stub device API
node tools/embed-portal.mjs   # regenerate firmware/.../svc/portal.h
```

## Why it is a file and not a C string

The page can be opened in a real browser, inspected, screenshotted and linted,
because it is real HTML. The generated header is what the firmware serves.
**Both are tracked** — building the firmware must never require node.

The alternative is editing markup inside C string escapes, which is where the
escaping bugs live.

## Why it is streamed, not built

The web server runs inside the same `loop()` that drives the display, so
response time *is* panel stall time. `sendContent` from PROGMEM in 1 KB chunks
costs no heap and bounds how long any single write blocks the panel;
assembling ~28 KB into a String first would not.

## What the device does not do

The full team catalog is ~120 KB. It never touches the device — the browser
fetches it straight from the proxy and caches it in `localStorage`. When the
browser can reach the panel but not the proxy (a phone on a guest VLAN), it
falls back to `/api/relay`, which is **path-allowlisted, never URL-allowlisted**:
accepting a host from a client would make it an SSRF primitive aimed at the
user's own LAN. That relay refuses with 503 while a score poll is in flight —
scores outrank setup.

## Testing it against the mock

`mock.mjs` serves a plausible device: nine games, a stale-free network, a
diagnostics payload, and a small two-league catalog on `/api/relay` so the
picker can be exercised with no proxy at all. The proxy URL in the mock config
is deliberately unreachable, which is what makes the relay fallback the
default path when you click **Load catalog** — the failure mode is the one
worth rehearsing.
