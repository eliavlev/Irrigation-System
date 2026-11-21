#!/bin/sh
# Copy Caddy's Let's Encrypt certificate for HOST into Mosquitto; restart Mosquitto if it changed.
set -eu
HOST="$(cat /etc/irrigation/host)"
SRC="$(find /var/lib/caddy/.local/share/caddy/certificates -type d -name "$HOST" -path '*letsencrypt*' 2>/dev/null | head -n1)"
[ -n "$SRC" ] && [ -f "$SRC/$HOST.crt" ] || { echo "certificate for $HOST not issued yet"; exit 1; }
DST=/etc/mosquitto/certs
mkdir -p "$DST"
if ! cmp -s "$SRC/$HOST.crt" "$DST/fullchain.pem" || ! cmp -s "$SRC/$HOST.key" "$DST/privkey.pem"; then
  install -m 644 -o root -g mosquitto "$SRC/$HOST.crt" "$DST/fullchain.pem"
  install -m 640 -o root -g mosquitto "$SRC/$HOST.key" "$DST/privkey.pem"
  [ -f /etc/mosquitto/conf.d/irrigation-tls.conf ] || cp /opt/irrigation/mosquitto/tls.conf /etc/mosquitto/conf.d/irrigation-tls.conf
  systemctl restart mosquitto
  echo "certificate updated, mosquitto restarted"
fi
