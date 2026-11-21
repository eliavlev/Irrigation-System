#!/bin/sh
# Manage dashboard users:  irrigation-user adduser|passwd|deluser|list [name] [--must-change]
# Runs auth.py as the service user so files in /var/lib/irrigation keep the right owner.
set -e
[ "$(id -u)" = 0 ] || { echo "run as root"; exit 1; }
set -a; . /etc/irrigation/auth.env; set +a
exec runuser -u irrigation -- env IRR_STATE_DIR="$IRR_STATE_DIR" IRR_HOST="$IRR_HOST" \
  ${IRR_NEW_PASSWORD:+IRR_NEW_PASSWORD="$IRR_NEW_PASSWORD"} /usr/bin/python3 /opt/irrigation/auth.py "$@"
