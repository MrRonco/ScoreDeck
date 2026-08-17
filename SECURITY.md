# Security

ScoreDeck is a LAN appliance: a panel on your desk and a proxy on a box you
control. It is not designed to be exposed to the internet.

## Reporting

Found something? Open a private security advisory on GitHub
(**Security → Report a vulnerability**) rather than a public issue.

## Posture (as of firmware v0.2.0)

A full third-party static review was run in August 2026 and every finding was
remediated; the report is archived at
[`docs/SECURITY-REVIEW-2026-08.md`](docs/SECURITY-REVIEW-2026-08.md). Highlights
of what the code now does:

- **The panel refuses privileged HTTP actions without a portal password.**
  Firmware updates (`/update`), Wi-Fi changes, and factory reset require a
  password to exist — an anonymous LAN client cannot flash or wipe the device.
  Set one on the panel or via the browser portal before you rely on OTA.
- **TLS is verified** against the built-in CA bundle by default; a self-signed
  LAN proxy needs the explicit `tlsInsecure` opt-out.
- **The proxy fails closed** without `SD_TOKEN` (set `SD_ALLOW_ANONYMOUS=1` to
  run it open on purpose).
- Portal auth is **Digest** with lockout; the portal has **no `innerHTML`
  sink** and pins its inline script by CSP hash; the flasher's `esp-web-tools`
  is **vendored and Subresource-Integrity-pinned**, not loaded from a CDN.

## Verify what you flash

The images attached to a release, and in `flasher/`, should match:

```
scoredeck-merged.bin  a2a3127a566bc30aaa41ed2016e5f8f3952bde6861ff7c41c2937d70cb953840
scoredeck-ota.bin     3c6762e3832c13590c2b42f87431973e6bfb09ec7c03a2581f73552e21332cd9
```

```bash
shasum -a 256 scoredeck-merged.bin
```
