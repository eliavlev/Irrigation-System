# Irrigation server (MQTT broker + dashboard)

Runs on a small Ubuntu/Debian droplet:

- **Mosquitto**: MQTT broker. The NodeMCU connects on port **8883** (TLS); the dashboard uses WebSockets via `/mqtt`.
- **Caddy**: automatic HTTPS (Let's Encrypt) and serves the dashboard from `/srv/irrigation`.
- **irrigation-certsync** (daily timer): copies Caddy's certificate to Mosquitto and restarts it when renewed.

## Install

1. Point a hostname (e.g. `yourname.duckdns.org`) at the droplet's public IP.
2. Copy this folder to the droplet and run as root:
   ```sh
   ./setup.sh yourname.duckdns.org
   ```
3. Credentials for the `garden` (device) and `web` (dashboard) MQTT users are written to
   `/root/irrigation-credentials.txt`. Put the `garden` password in `Watering_System/secrets.h`.
4. Open `https://yourname.duckdns.org/` and log in as `web`.

Ports opened: 80, 443 (Caddy), 8883 (MQTT/TLS). If the droplet uses a DigitalOcean cloud firewall, allow those there too.

## Useful commands (on the droplet)

```sh
mosquitto_sub -h 127.0.0.1 -u web -P '<web password>' -t 'irrigation/#' -v   # watch all traffic
journalctl -u mosquitto -f                                                      # broker log
systemctl status caddy irrigation-certsync.timer
mosquitto_passwd /etc/mosquitto/passwd web && systemctl restart mosquitto       # change a password
```
