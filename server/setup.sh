#!/bin/bash
# Installs the irrigation MQTT broker + dashboard on an Ubuntu/Debian droplet.
# Usage (as root, from the server/ folder):  ./setup.sh yourname.duckdns.org
set -euo pipefail
HOST="${1:?usage: $0 <hostname, e.g. yourname.duckdns.org>}"
HERE="$(cd "$(dirname "$0")" && pwd)"
[ "$(id -u)" = 0 ] || { echo "run as root"; exit 1; }

# --- refuse to clobber an existing web server on 80/443
if ss -ltnpH '( sport = :80 or sport = :443 )' | grep -v caddy | grep -q .; then
  echo "Ports 80/443 are already used by another program:"; ss -ltnp '( sport = :80 or sport = :443 )'
  echo "Aborting so nothing existing is broken."; exit 2
fi

# --- packages
export DEBIAN_FRONTEND=noninteractive
apt-get update -q
apt-get install -y -q mosquitto mosquitto-clients debian-keyring debian-archive-keyring apt-transport-https curl gnupg openssl
if ! command -v caddy >/dev/null; then
  curl -1sLf https://dl.cloudsmith.io/public/caddy/stable/gpg.key | gpg --dearmor --yes -o /usr/share/keyrings/caddy-stable-archive-keyring.gpg
  curl -1sLf https://dl.cloudsmith.io/public/caddy/stable/debian.deb.txt > /etc/apt/sources.list.d/caddy-stable.list
  apt-get update -q && apt-get install -y -q caddy
fi

# --- files
mkdir -p /etc/irrigation /opt/irrigation /srv/irrigation
echo "$HOST" > /etc/irrigation/host
cp -r "$HERE/mosquitto" /opt/irrigation/
install -m 755 "$HERE/certsync.sh" /usr/local/bin/irrigation-certsync
cp "$HERE/dashboard/"* /srv/irrigation/
cp "$HERE/mosquitto/irrigation.conf" /etc/mosquitto/conf.d/irrigation.conf
cp "$HERE/mosquitto/acl" /etc/mosquitto/acl
chown root:mosquitto /etc/mosquitto/acl && chmod 640 /etc/mosquitto/acl

# --- MQTT users (keep existing passwords on re-runs)
CRED=/root/irrigation-credentials.txt
touch /etc/mosquitto/passwd
for U in garden web; do
  if ! grep -q "^$U:" /etc/mosquitto/passwd; then
    P="$(openssl rand -base64 18 | tr -d '/+=' | cut -c1-20)"
    mosquitto_passwd -b /etc/mosquitto/passwd "$U" "$P"
    echo "$U=$P" >> "$CRED"
  fi
done
chown root:mosquitto /etc/mosquitto/passwd && chmod 640 /etc/mosquitto/passwd
chmod 600 "$CRED"
systemctl enable mosquitto && systemctl restart mosquitto

# --- Caddy (HTTPS + dashboard + /mqtt websocket proxy)
# Adds a site block for $HOST; any existing sites in the Caddyfile are left untouched.
CF=/etc/caddy/Caddyfile
if [ -f "$CF" ] && grep -q "The Caddyfile is an easy way" "$CF"; then : > "$CF"; fi   # stock example file only
BAK=""
if ! grep -qF "$HOST {" "$CF" 2>/dev/null; then
  if [ -s "$CF" ]; then BAK="$CF.bak.$(date +%Y%m%d%H%M%S)"; cp "$CF" "$BAK"; fi
  { echo; sed "s/__HOST__/$HOST/" "$HERE/Caddyfile"; } >> "$CF"
fi
if ! caddy validate --config "$CF" --adapter caddyfile; then
  [ -n "$BAK" ] && cp "$BAK" "$CF" && echo "Caddyfile invalid - original restored from $BAK"
  exit 4
fi
systemctl enable caddy && systemctl reload-or-restart caddy

# --- firewall (only if ufw is active)
if command -v ufw >/dev/null && ufw status | grep -q "Status: active"; then
  ufw allow 80/tcp; ufw allow 443/tcp; ufw allow 8883/tcp
fi

# --- keep Mosquitto's TLS cert in sync with Caddy's
cat > /etc/systemd/system/irrigation-certsync.service <<UNIT
[Unit]
Description=Copy Caddy certificate to Mosquitto
[Service]
Type=oneshot
ExecStart=/usr/local/bin/irrigation-certsync
UNIT
cat > /etc/systemd/system/irrigation-certsync.timer <<UNIT
[Unit]
Description=Daily certificate sync for Mosquitto
[Timer]
OnBootSec=2min
OnUnitActiveSec=1d
Persistent=true
[Install]
WantedBy=timers.target
UNIT
systemctl daemon-reload
systemctl enable --now irrigation-certsync.timer

echo "Waiting for Let's Encrypt certificate for $HOST ..."
for i in $(seq 1 36); do
  if /usr/local/bin/irrigation-certsync >/dev/null 2>&1; then break; fi
  sleep 5
done
/usr/local/bin/irrigation-certsync || { echo "No certificate yet - check that $HOST points at this droplet, then run irrigation-certsync"; }

echo
echo "Done. Dashboard: https://$HOST/"
echo "MQTT credentials are in $CRED"
ss -ltn | grep -E ':(443|8883|9001|1883)\b' || true
