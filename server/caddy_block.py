#!/usr/bin/env python3
"""Put the irrigation site block into a Caddyfile, replacing an earlier copy of it.

Usage: caddy_block.py <Caddyfile> <host> <template>
Only the block between '# BEGIN irrigation' and '# END irrigation' (or the unmarked
block written by the first version of setup.sh) is touched; other sites are kept.
"""
import re
import sys

cf, host, tpl = sys.argv[1:4]
try:
    with open(cf, encoding="utf-8") as f:
        text = f.read()
except FileNotFoundError:
    text = ""

text = re.sub(r"\n*# BEGIN irrigation\n.*?# END irrigation\n?", "\n", text, flags=re.S)
legacy = (r"\n*# Irrigation dashboard \+ MQTT-over-WebSocket proxy[^\n]*\n"
          + re.escape(host) + r" \{\n.*?\n\}\n?")
text = re.sub(legacy, "\n", text, flags=re.S)

with open(tpl, encoding="utf-8") as f:
    block = f.read().replace("__HOST__", host)
with open(cf, "w", encoding="utf-8") as f:
    f.write((text.rstrip("\n") + "\n\n" if text.strip() else "") + block)
