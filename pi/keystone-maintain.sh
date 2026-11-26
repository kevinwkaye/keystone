#!/bin/bash
# Keystone on the Pi: keeps the Pi's own helpers in step with the Keystone version running.
# Run as root by keystone-wifi.sh, which runs each time Keystone starts (it writes wifi.json).
# Pis set up before 0.6.0 run the updater they were set up with, which only refreshes the
# Wi-Fi helper, so this is how newer helpers and the projector's video player arrive.
set -u
ROOT="${KS_ROOT:-/opt/keystone}"
CUR="$ROOT/current"
LOG="${KS_HOME:-/home/keystone/Documents/Keystone}/Updates/update.log"
log() { echo "$(date '+%Y-%m-%d %H:%M:%S')  $*" >> "$LOG" 2>/dev/null; }
[ -d "$CUR/pi" ] || exit 0

# the updater: the newest one, for the next Update firmware
if [ -f "$CUR/pi/keystone-update.sh" ] && ! cmp -s "$CUR/pi/keystone-update.sh" "$ROOT/keystone-update.sh"; then
    install -m 755 "$CUR/pi/keystone-update.sh" "$ROOT/keystone-update.sh" && log "Updated the updater"
fi
# Keystone's service settings (0.6.0 adds the render group for the projector; it applies from the next start)
if [ -f "$CUR/pi/keystone.service" ] && ! cmp -s "$CUR/pi/keystone.service" /etc/systemd/system/keystone.service; then
    install -m 644 "$CUR/pi/keystone.service" /etc/systemd/system/keystone.service && systemctl daemon-reload && log "Updated Keystone's service settings"
fi
# the projector's video player, carried in the package (no internet on Keystone's Wi-Fi)
if [ -d "$CUR/pi/debs" ]; then
    if ! command -v mpv >/dev/null 2>&1; then
        if DEBIAN_FRONTEND=noninteractive apt-get install -y -q --no-download --no-install-recommends "$CUR"/pi/debs/*.deb >> "$LOG" 2>&1; then
            log "Installed the video player for the projector"
        else
            log "Couldn't install the video player"
        fi
    fi
    command -v mpv >/dev/null 2>&1 && rm -rf "$(readlink -f "$CUR")/pi/debs"
fi
exit 0
