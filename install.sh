#!/usr/bin/env bash
# ScoreDeck proxy launcher.
#
#   curl -fsSL https://raw.githubusercontent.com/MrRonco/ScoreDeck/main/install.sh | bash
#
# Installs (or updates) the ScoreDeck proxy as a Docker container on this
# machine. Idempotent: re-running updates the code and rebuilds; your token
# and .env survive. Works from inside a clone too — it detects that and
# skips the download.
#
# The panel needs two things when this finishes: the URL and the token this
# script prints. Nothing else to configure.
set -euo pipefail

REPO="https://github.com/MrRonco/ScoreDeck.git"
DIR="${SCOREDECK_DIR:-$HOME/scoredeck}"

say()  { printf '\033[1;36m▸ %s\033[0m\n' "$*"; }
fail() { printf '\033[1;31m✗ %s\033[0m\n' "$*" >&2; exit 1; }

# ── prerequisites ───────────────────────────────────────────────────────────
command -v docker >/dev/null 2>&1 \
  || fail "Docker is required. Install it first: https://docs.docker.com/engine/install/"
docker compose version >/dev/null 2>&1 \
  || fail "Docker Compose v2 is required (the 'docker compose' subcommand)."
docker info >/dev/null 2>&1 \
  || fail "Docker is installed but not running (or you need to be in the docker group)."

# ── fetch or update ─────────────────────────────────────────────────────────
if [ -f "$(dirname "$0")/proxy/docker-compose.yml" ] 2>/dev/null; then
  # Running from inside a checkout (e.g. ./install.sh) — use it as-is.
  DIR="$(cd "$(dirname "$0")" && pwd)"
  say "Using this checkout: $DIR"
elif [ -d "$DIR/.git" ]; then
  say "Updating existing install in $DIR"
  git -C "$DIR" pull --ff-only
else
  command -v git >/dev/null 2>&1 || fail "git is required to download ScoreDeck."
  say "Cloning ScoreDeck into $DIR"
  git clone --depth 1 "$REPO" "$DIR"
fi

cd "$DIR/proxy"

# ── token ───────────────────────────────────────────────────────────────────
if [ -f .env ] && grep -q '^SD_TOKEN=..*' .env; then
  say "Keeping the existing token in proxy/.env"
else
  TOKEN="$( (command -v openssl >/dev/null && openssl rand -hex 24) \
            || head -c 24 /dev/urandom | od -An -tx1 | tr -d ' \n')"
  printf 'SD_TOKEN=%s\nTZ=%s\n' "$TOKEN" "$(cat /etc/timezone 2>/dev/null || echo UTC)" > .env
  say "Generated a new auth token in proxy/.env"
fi

# ── build & start ───────────────────────────────────────────────────────────
say "Building and starting the container (first build takes a minute)…"
docker compose up -d --build

# ── verify ──────────────────────────────────────────────────────────────────
TOKEN="$(grep '^SD_TOKEN=' .env | cut -d= -f2)"
say "Waiting for the proxy to answer…"
for _ in $(seq 1 30); do
  if curl -fsS -m 3 -H "Authorization: Bearer $TOKEN" \
       http://localhost:8787/v1/health >/dev/null 2>&1; then
    OK=1; break
  fi
  sleep 1
done
[ "${OK:-}" = 1 ] || fail "The container started but /v1/health never answered. Check: docker logs scoredeck-proxy"

IP="$(hostname -I 2>/dev/null | awk '{print $1}')"
[ -n "$IP" ] || IP="$(ipconfig getifaddr en0 2>/dev/null || echo '<this-host>')"

cat <<EOF

$(printf '\033[1;32m✓ ScoreDeck proxy is running.\033[0m')

  On the panel (or at http://scoredeck.local/ once it is on your Wi-Fi):

      Proxy URL :  http://$IP:8787
      Token     :  $TOKEN

  Useful commands:

      docker logs -f scoredeck-proxy          follow the proxy
      cd $DIR && ./install.sh                 update to the latest version
      cd $DIR/proxy && npm run logos          build team logo packs (optional,
                                              trademarked art stays on YOUR box)

  Panel on an isolated IoT VLAN that can't reach this machine?
  See docs/DEPLOY.md — docker-compose.unraid.yml puts the container
  directly on the panel's VLAN with its own IP.

EOF
