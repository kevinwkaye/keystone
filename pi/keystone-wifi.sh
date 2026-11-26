#!/bin/bash
# Keystone's own Wi-Fi on the Pi: an access point called Keystone that the phone joins.
# Keystone writes ~/Documents/Keystone/wifi.json (on, name, password); keystone-wifi.path
# notices and runs this as root. NetworkManager keeps the network up from then on, at boot
# too. 5 GHz first (quieter on a stage full of phones), the 2.4 GHz band if that won't start.
set -u
HOME_DIR="${KS_HOME:-/home/keystone/Documents/Keystone}"
REQ="$HOME_DIR/wifi.json"
STATUS="$HOME_DIR/wifi-status.txt"
CON=keystone-ap
say() { echo "$*" > "$STATUS"; chown keystone:keystone "$STATUS" 2>/dev/null; }
field() { python3 -c 'import json,sys; v=json.load(open(sys.argv[1])).get(sys.argv[2]); print(str(v).lower() if isinstance(v,bool) else v)' "$REQ" "$1" 2>/dev/null; }

# the Pi's other helpers and the projector's video player, kept in step (0.6.0)
[ -f "${KS_ROOT:-/opt/keystone}/current/pi/keystone-maintain.sh" ] && [ -z "${KS_HOME:-}" ] && bash "${KS_ROOT:-/opt/keystone}/current/pi/keystone-maintain.sh"

[ -f "$REQ" ] || exit 0
ON="$(field on)"; SSID="$(field ssid)"; PASS="$(field pass)"
command -v nmcli >/dev/null || { say "Off: NetworkManager isn't on this Pi"; exit 1; }

if [ "$ON" != "true" ]; then
    nmcli con down "$CON" >/dev/null 2>&1
    nmcli con delete "$CON" >/dev/null 2>&1
    say "Off"
    exit 0
fi
[ ${#PASS} -ge 8 ] || { say "Off: the password is too short"; exit 1; }

# Local only (0.5.7): the network hands out addresses with no internet route and no DNS, so
# phones treat it as a local network and keep mobile data for everything else. Without this
# Android routed all its traffic into Keystone's Wi-Fi, which goes nowhere.
LOCAL=/etc/NetworkManager/dnsmasq-shared.d/keystone-local.conf
WANT_LOCAL='# Keystone Wi-Fi: no default route, no DNS, so phones keep mobile data for the internet
dhcp-option=3
dhcp-option=6'
LOCAL_CHANGED=0
if [ "$(cat "$LOCAL" 2>/dev/null)" != "$WANT_LOCAL" ]; then
    mkdir -p "$(dirname "$LOCAL")"
    printf '%s\n' "$WANT_LOCAL" > "$LOCAL"
    LOCAL_CHANGED=1
fi

# already running with these settings: leave it be (Keystone writes the file each time it starts),
# unless the local-only setting just went in, which needs the network brought up again
if nmcli -t -f GENERAL.STATE con show "$CON" 2>/dev/null | grep -q activated \
   && [ "$(nmcli -s -g 802-11-wireless.ssid con show "$CON")" = "$SSID" ] \
   && [ "$(nmcli -s -g 802-11-wireless-security.psk con show "$CON")" = "$PASS" ]; then
    if [ "$LOCAL_CHANGED" = 1 ]; then
        nmcli con down "$CON" >/dev/null 2>&1
        nmcli con up "$CON" >/dev/null 2>&1
    fi
    say "On: $SSID"
    exit 0
fi

make() {   # band: a (5 GHz) or bg (2.4 GHz)
    nmcli con delete "$CON" >/dev/null 2>&1
    nmcli con add type wifi ifname wlan0 con-name "$CON" autoconnect yes ssid "$SSID" \
        802-11-wireless.mode ap 802-11-wireless.band "$1" \
        ipv4.method shared ipv4.addresses 10.42.0.1/24 ipv6.method disabled \
        wifi-sec.key-mgmt wpa-psk wifi-sec.psk "$PASS" \
        connection.autoconnect-priority 100 >/dev/null && nmcli con up "$CON" >/dev/null 2>&1
}
if make a; then say "On: $SSID (5 GHz)"
elif make bg; then say "On: $SSID (2.4 GHz)"
else say "Off: the Pi's Wi-Fi wouldn't start the network"; exit 1
fi
