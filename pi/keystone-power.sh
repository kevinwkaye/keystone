#!/bin/bash
# The service button's shut down (0.6.2). Keystone writes ~/Documents/Keystone/power.json when
# you hold the button on the back for 3 seconds and let go; keystone-power.path notices and runs
# this as root. Only a fresh request counts, so an old file left over from before a boot can't
# switch the Pi off again.
set -u
REQ="${KS_HOME:-/home/keystone/Documents/Keystone}/power.json"
LOG="${KS_HOME:-/home/keystone/Documents/Keystone}/Updates/update.log"
[ -f "$REQ" ] || exit 0
WHAT="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("do",""))' "$REQ" 2>/dev/null)"
AGE=$(( $(date +%s) - $(stat -c %Y "$REQ") ))
rm -f "$REQ"
[ "$WHAT" = "off" ] && [ "$AGE" -lt 60 ] || exit 0
echo "$(date '+%Y-%m-%d %H:%M:%S')  Shut down from the service button" >> "$LOG" 2>/dev/null
systemctl poweroff
