# Irrigation server (MQTT broker + dashboard)

Runs on a small Ubuntu/Debian droplet:

- **Mosquitto**: MQTT broker. The NodeMCU connects on port **8883** (TLS); the dashboard uses WebSockets via `/mqtt`.
- **Caddy**: automatic HTTPS (Let's Encrypt) and serves the dashboard from `/srv/irrigation`.
- **irrigation-certsync** (daily timer): copies Caddy's certificate to Mosquitto and restarts it when renewed.
- **irrigation-auth** (`auth.py`, Python stdlib only): dashboard logins. Passwords are stored as salted scrypt
  hashes in `/var/lib/irrigation/users.json`; sessions are signed HttpOnly cookies. Caddy only lets logged-in
  users reach the broker's WebSocket (`/mqtt`), so the browser never holds an MQTT password.

## Install

1. Point a hostname (e.g. `yourname.duckdns.org`) at the droplet's public IP.
2. Copy this folder to the droplet and run as root:
   ```sh
   ./setup.sh yourname.duckdns.org
   ```
3. Credentials for the `garden` (device) and `web` (command-line) MQTT users are written to
   `/root/irrigation-credentials.txt`. Put the `garden` password in `Watering_System/secrets.h`.
4. Create a dashboard user (asked to choose a new password at first login):
   ```sh
   irrigation-user adduser <name> --must-change
   ```
5. Open `https://yourname.duckdns.org/` and log in.

Re-running `setup.sh` is safe: it keeps users and MQTT passwords and only replaces its own Caddy site block
(between `# BEGIN irrigation` and `# END irrigation`).

Ports opened: 80, 443 (Caddy), 8883 (MQTT/TLS). If the droplet uses a DigitalOcean cloud firewall, allow those there too.

## Useful commands (on the droplet)

```sh
mosquitto_sub -h 127.0.0.1 -u web -P '<web password>' -t 'irrigation/#' -v   # watch all traffic
journalctl -u mosquitto -f                                                      # broker log
systemctl status caddy irrigation-certsync.timer
mosquitto_passwd /etc/mosquitto/passwd web && systemctl restart mosquitto       # change an MQTT password
irrigation-user list | adduser <name> [--must-change] | passwd <name> | deluser <name>   # dashboard users
journalctl -u irrigation-auth -f                                                # login service log
```
