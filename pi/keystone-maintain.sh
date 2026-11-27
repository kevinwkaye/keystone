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
# the service button on the back (0.6.2): its shut down helper, and the gpio group so Keystone
# can read pin 37. Keystone only picks up a new group when it starts, so it's restarted once
# here, a moment after it started anyway (this runs each time Keystone starts)
if [ -f "$CUR/pi/keystone-power.sh" ]; then
    if ! cmp -s "$CUR/pi/keystone-power.sh" "$ROOT/keystone-power.sh" \
       || ! cmp -s "$CUR/pi/keystone-power.path" /etc/systemd/system/keystone-power.path \
       || ! cmp -s "$CUR/pi/keystone-power.service" /etc/systemd/system/keystone-power.service; then
        install -m 755 "$CUR/pi/keystone-power.sh" "$ROOT/keystone-power.sh"
        install -m 644 "$CUR/pi/keystone-power.path" /etc/systemd/system/keystone-power.path
        install -m 644 "$CUR/pi/keystone-power.service" /etc/systemd/system/keystone-power.service
        systemctl daemon-reload; systemctl enable --now keystone-power.path >/dev/null 2>&1
        log "Set up the service button's shut down"
    fi
    if getent group gpio >/dev/null && ! id -nG keystone 2>/dev/null | grep -qw gpio; then
        usermod -aG gpio keystone && log "Keystone can read the service button now (gpio group), restarting it once" \
            && systemctl restart --no-block keystone
    fi
fi
# Send report: the systemd-journal group lets Keystone put the system journal and the kernel
# log in a report. No restart for this one: it applies from the next time Keystone starts.
if getent group systemd-journal >/dev/null && ! id -nG keystone 2>/dev/null | grep -qw systemd-journal; then
    usermod -aG systemd-journal keystone && log "Keystone can read the system journal for reports from its next start"
fi
# the projector's video player, carried in the package (no internet on Keystone's Wi-Fi).
# 0.6.3: it lives in pi/player, and stays there until it's in, so a failed try is tried
# again at the next start. Keystone shows what happened from player.txt.
PLAYER="$CUR/pi/player"; [ -d "$PLAYER" ] || PLAYER="$CUR/pi/debs"
STATE="$(dirname "$LOG")/player.txt"
state() { mkdir -p "$(dirname "$STATE")"; echo "$*" > "$STATE"; chown keystone:keystone "$STATE" 2>/dev/null; }
if [ -d "$PLAYER" ] && ls "$PLAYER"/*.deb >/dev/null 2>&1; then
    if ! [ -x /usr/bin/mpv ]; then
        state "installing"
        export DEBIAN_FRONTEND=noninteractive
        # a half-finished install from before (a power cut, say) has to be finished off first
        dpkg --configure -a >> "$LOG" 2>&1
        # 0.6.4: dpkg straight from the carried files. apt (0.6.0 to 0.6.3) saw the same versions
        # in its own lists and insisted on downloading them, which can't work on Keystone's
        # Wi-Fi. The set is complete for any Keystone Pi (checked on the 0.5.5 image).
        # If something else holds the package lock, wait for it, up to three minutes.
        OUT=""
        for i in $(seq 1 18); do
            OUT="$(dpkg -i --skip-same-version "$PLAYER"/*.deb 2>&1)"
            echo "$OUT" >> "$LOG"
            echo "$OUT" | grep -qi "lock" || break
            sleep 10
        done
        OUT2=""
        if [ -x /usr/bin/mpv ]; then
            log "Installed the video player for the projector"
        else
            WHY="$(printf '%s\n%s\n' "$OUT" "$OUT2" | grep -E '^(E:|dpkg: error|dpkg: dependency problems| [a-z0-9.+-]+ depends on)' | head -3 | tr '\n' ' ' | cut -c1-300)"
            state "failed: ${WHY:-dpkg gave no reason, see Updates/update.log}"
            log "Couldn't install the video player: ${WHY:-no reason given}"
            exit 0
        fi
    fi
    state "ok"
    rm -rf "$(readlink -f "$PLAYER")"
fi
exit 0
