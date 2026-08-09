# Deploying the proxy

ScoreDeck's panel makes **one call per poll** to a proxy you run. There is no
shared instance — see [`OPEN_SOURCE.md`](OPEN_SOURCE.md) §2 for why.

Pick one of three paths. They all run the same code.

| Path | Best when | Cost | CPU limits | Works away from home |
|---|---|---|---|---|
| **A — Unraid / NAS / Pi** | You already run a box that is always on | £0 | none | No |
| **B — Cloudflare Workers** | The panel must work off your network | £0 (maybe $5/mo) | 10 ms/request | Yes |
| **C — Laptop** | Development only | £0 | none | No |

**For a desk panel, self-hosting is usually the better call.** Cloudflare's real
advantage is working away from home, and a 7-inch display never leaves the desk.
Meanwhile the Workers free tier caps CPU at 10 ms per request, which is the one
unresolved risk for the golf and tennis normalisers (1.2 MB and 658 KB
payloads). On your own hardware that risk simply does not exist — every league
is viable on day one.

---

## Measured network facts (this installation, 2026-08-09)

Worth recording, because they decide which path is viable:

| From → to | Result |
|---|---|
| Panel `192.168.20.161` → Mac `192.168.10.100` any port | **Blocked** — IoT VLAN cannot initiate to the main LAN |
| Panel → feeder Pi `192.168.15.20:8080` | **Reachable** — tar1090 answered HTTP 404 |
| Panel → feeder Pi `192.168.15.20:8787` | Refused in 5 ms — a TCP reset, i.e. the packet arrived and nothing was listening. A firewall drop times out instead (the first 8080 probe took 11 s). **So other ports on the Pi should work.** |
| Panel → internet, HTTP and HTTPS | **Works.** TLS handshake completes in ~600 ms |

Conclusion: **Path A on the existing feeder Pi is the least friction.** Path B is
the real product path and the one to use if the panel should keep working when
the Pi is down.

---

## Path A — Unraid (recommended here)

Unraid is the best fit if you have it: always on, proper Docker management, and
none of the CPU ceilings. The container is ~60 MB and idles at close to nothing.

### Check reachability first

This is the only thing that can rule Unraid out. Your panel sits on the IoT VLAN
and **cannot reach the main LAN** — so if Unraid lives on the same subnet as your
Mac, it needs a firewall rule just as the Mac would. Test it from the panel over
USB before you build anything:

```
proxy http://<UNRAID-IP>:8787
```

Read the failure on serial:

- **`HTTP -1` in a few milliseconds** → the packet arrived and nothing is
  listening. Reachable. Carry on.
- **`HTTP -1` after seconds** → the firewall is dropping it. You need a pass rule
  from the panel's VLAN to `<UNRAID-IP>:8787`, or put the container on a
  reachable network.

### Option 1 — Compose Manager plugin (cleanest)

Install **Compose Manager** from Community Applications, then add a stack:

```bash
# on Unraid, via terminal
mkdir -p /mnt/user/appdata/scoredeck && cd /mnt/user/appdata/scoredeck
git clone https://github.com/MrRonco/ScoreDeck.git .
cd proxy
echo "SD_TOKEN=$(openssl rand -hex 24)" > .env
echo "TZ=America/Toronto" >> .env
docker compose up -d --build
cat .env          # copy the token, the panel needs it
```

### Option 2 — Unraid Docker tab, no compose

Once the image is published you can add it as a container without cloning
anything:

| Field | Value |
|---|---|
| Repository | `ghcr.io/mrronco/scoredeck-proxy:latest` |
| Network Type | `Bridge` |
| Port | Container `8787` → Host `8787` |
| Variable | `SD_TOKEN` = your token |
| Variable | `TZ` = e.g. `America/Toronto` |

> While the repository is private the image is private too, so Unraid needs a
> one-off `docker login ghcr.io` with a GitHub personal access token that has
> `read:packages`. Option 1 avoids that entirely by building locally.

The container declares a `HEALTHCHECK`, so Unraid's health dot reflects whether
the proxy is actually answering rather than just running.

### Why this beats Cloudflare for this device

- **No CPU ceiling** — golf and tennis normalise fine, and the whole
  free-vs-$5/mo question disappears.
- **No cron needed.** The Worker warms a cache every 2 minutes to dodge the 10 ms
  budget; self-hosted, the request path just does the work.
- **No account, no vendor**, and your viewing habits stay on your own hardware.

The trade: it is one more thing you maintain, and the panel goes blank if the
server is down. The device degrades honestly when that happens — it shows the
failure and keeps retrying rather than hanging.

---

## Path A2 — Raspberry Pi

The Pi already running adsb.im/tar1090 is fine; the proxy is a few MB of Node
and idles at almost nothing. **Measured: your panel can already reach this
host** (see the table above), which makes it the zero-firewall-change option.

### 1. Install Node 22+

```bash
ssh pi@192.168.15.20
node -v      # need v22 or newer for --experimental-strip-types
# if missing or older:
curl -fsSL https://deb.nodesource.com/setup_22.x | sudo -E bash -
sudo apt-get install -y nodejs
```

### 2. Get the code

```bash
git clone https://github.com/MrRonco/ScoreDeck.git ~/scoredeck
cd ~/scoredeck/proxy
npm install --omit=dev
```

### 3. Make a token

The proxy is reachable by anything on your network. It is not a bank, but an
open proxy in front of a third-party API is a thing people find.

```bash
openssl rand -hex 24        # copy this — the panel needs it too
```

### 4. Run it as a service

```bash
sudo tee /etc/systemd/system/scoredeck.service >/dev/null <<'UNIT'
[Unit]
Description=ScoreDeck proxy
After=network-online.target

[Service]
Type=simple
User=pi
WorkingDirectory=/home/pi/scoredeck/proxy
Environment=PORT=8787
Environment=SD_TOKEN=PASTE_YOUR_TOKEN_HERE
ExecStart=/usr/bin/node --experimental-strip-types src/server.ts
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
UNIT

sudo systemctl daemon-reload
sudo systemctl enable --now scoredeck
systemctl status scoredeck --no-pager
```

Docker instead, if you prefer:

```bash
cd ~/scoredeck/proxy
echo "SD_TOKEN=$(openssl rand -hex 24)" > .env
docker compose up -d
```

### 5. Check it from your laptop

```bash
curl -H "Authorization: Bearer YOUR_TOKEN" http://192.168.15.20:8787/v1/health
# {"ok":true,"leagues":20,"now":...}
```

If that works but the panel still cannot reach it, add a firewall rule
permitting **TCP from the IoT VLAN → 192.168.15.20:8787** — the same shape as the
rule that already lets the panel reach tar1090 on 8080.

---

## Path B — Cloudflare Workers

Free tier, works from anywhere, and it is what the project documents as the
production path.

### 1. Sign in and deploy

```bash
cd proxy
npm install
npx wrangler login                       # opens a browser
npx wrangler secret put SD_TOKEN         # paste a token from `openssl rand -hex 24`
npx wrangler deploy
```

Wrangler prints the URL, e.g. `https://scoredeck.<your-subdomain>.workers.dev`.

### 2. Verify

```bash
curl -H "Authorization: Bearer YOUR_TOKEN" \
  https://scoredeck.<your-subdomain>.workers.dev/v1/health
```

### What is already configured

`wrangler.toml` sets a **cron every 2 minutes** that warms the board cache. That
matters: the free plan allows **10 ms CPU per request**, so the heavy work of
fetching and normalising happens in the cron, and a device request becomes a
cache read plus a filter.

Storage is the **Cache API, not KV** — deliberately. KV free tier allows 1,000
writes/day and a 2-minute cron across five leagues would exhaust that before
lunch. The Cache API has no write cap.

### The one thing still unproven

**The golf and tennis normalisers have never been measured on Workers.** Their
scoreboards are 1.2 MB and 658 KB. If a scheduled invocation is also capped at
10 ms on the free plan, those two leagues will fail there — everything else is
small enough not to care. Two ways out: take the **$5/mo Workers Paid** plan
(30 s CPU), or leave golf and tennis off and stay free. Measure before deciding:

```bash
npx wrangler tail          # then watch CPU on the cron invocations
```

---

## Path C — laptop, development only

```bash
cd proxy
npm install
SD_TOKEN=$(openssl rand -hex 24) npm run dev
# scoredeck-proxy on http://0.0.0.0:8787
```

**On this network the panel cannot reach a laptop on `192.168.10.x`.** Either add a
firewall rule from the IoT VLAN to your laptop's IP on 8787, or move the laptop
onto a reachable segment. A tunnel (`npx localtunnel --port 8787`) also works
and is what was used for the first end-to-end test, but it is not something to
leave running.

---

## Pointing the panel at it

Two ways. The serial console is faster while iterating.

### Over USB

```bash
arduino-cli monitor -p /dev/cu.wchusbserial* -c baudrate=115200
```

Then type:

```
token  <your token>
proxy  http://192.168.15.20:8787
favs   nhl:21,mlb:14
lgs    nhl,mlb,nfl
region ca
show
```

Each command saves to NVS and triggers an immediate poll. `games` dumps the
board; `shot` prints a low-res render of the live screen.

### On the panel

First boot shows the Wi-Fi screen with a proxy field. If you are coming from
AirRadar the Wi-Fi is imported automatically and you only need the proxy URL.

---

## Finding your team IDs

`favs` takes `league:teamId`. Look them up:

```bash
curl -s -H "Authorization: Bearer YOUR_TOKEN" \
  http://192.168.15.20:8787/v1/teams/nhl | jq -r '.[] | "\(.a)\t\(.id)\t\(.n)"'

# ANA  25  Anaheim Ducks
# BOS   1  Boston Bruins
# TOR  21  Toronto Maple Leafs
# ...
```

League slugs come from `/v1/catalog`. The ones you are most likely to want:

| Slug | League | | Slug | League |
|---|---|---|---|---|
| `nhl` | NHL | | `eng.1` | Premier League |
| `nfl` | NFL | | `ucl` | Champions League |
| `nba` | NBA | | `mls` | MLS |
| `mlb` | MLB | | `nwsl` | NWSL |

**IDs are only unique within a sport** — `nhl:21` is Toronto, `mlb:21` is not.
Always include the league prefix.

---

## Verifying end to end

```bash
curl -s -H "Authorization: Bearer YOUR_TOKEN" \
  "http://192.168.15.20:8787/v1/state?lg=nhl,mlb&f=nhl:21&rgn=ca&tz=America/Toronto" | jq '
  {games: (.games|length), live: [.games[]|select(.g==1)]|length,
   next: .next, stale: .stale, first: .games[0]}'
```

Expect a response **around 3 KB**, `next` between 12 and 300 seconds, and any
followed team sorted to the front with `"f": true`.

On the panel, `[net] poll ok in NNN ms games=15` on serial means it is working.

---

## Troubleshooting

| Symptom | Cause |
|---|---|
| `proxy HTTP -1` **fast** (< 50 ms) | Nothing listening on that port — proxy not running, or wrong port |
| `proxy HTTP -1` **slow** (seconds) | Firewall dropping the packet. Add a pass rule from the panel's VLAN |
| `proxy HTTP 401` | Token mismatch. `token <t>` on the panel must equal `SD_TOKEN` on the proxy |
| `proxy HTTP 404` | Something else is listening there (tar1090 gives this on `:8080`) |
| `schema vN (NNNN B)` | The proxy returned JSON that is not ours — usually a tunnel or captive-portal interstitial |
| `stale: true` in the payload | ESPN is failing upstream. The proxy serves last-known-good rather than blanking the board |
| Board empty, no errors | No games today for the leagues you follow. Try `lgs mlb` in season |

**Do not put the proxy behind Cloudflare Access or any login page.** The panel
sends a bearer token and nothing else; an interstitial will come back as
`schema v0`.
