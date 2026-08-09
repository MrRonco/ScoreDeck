#!/bin/bash
# Ensure the ScoreDeck proxy is reachable from the panel's VLAN after a reboot.
#
# The panel lives on VLAN 20 and cannot reach the trusted LAN, so the container
# takes its own address on br0.20 (L2 adjacency — no firewall rule, nothing
# crosses OPNsense). Unraid does not reliably recreate a per-VLAN docker network
# when the host holds no IP on that VLAN, so recreate it here before starting.
#
# Idempotent. Same pattern as fix-vlan15-containers.sh.
# Added by Claude Code - 2026-08-09
set -u

LOG() { logger -t scoredeck "$*"; }
DIR=/mnt/user/appdata/scoredeck
NET=br0.20
PARENT=br0.20
SUBNET=192.168.20.0/24
GATEWAY=192.168.20.1

# Wait for the docker daemon (array start can be slow).
for _ in $(seq 1 60); do
  docker info >/dev/null 2>&1 && break
  sleep 5
done
if ! docker info >/dev/null 2>&1; then
  LOG "docker never came up; giving up"
  exit 0
fi

# Wait for the VLAN interface Unraid brings up from network.cfg.
for _ in $(seq 1 30); do
  ip link show "$PARENT" >/dev/null 2>&1 && break
  sleep 2
done

if ! docker network inspect "$NET" >/dev/null 2>&1; then
  if docker network create -d ipvlan -o parent="$PARENT" \
       --subnet "$SUBNET" --gateway "$GATEWAY" "$NET" >/dev/null 2>&1; then
    LOG "created docker network $NET"
  else
    LOG "FAILED to create docker network $NET"
    exit 0
  fi
fi

if [ -f "$DIR/docker-compose.unraid.yml" ]; then
  cd "$DIR" || exit 0
  if docker compose -f docker-compose.unraid.yml up -d >/dev/null 2>&1; then
    LOG "proxy up"
  else
    LOG "compose up FAILED"
  fi
fi
