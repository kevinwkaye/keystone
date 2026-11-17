#!/bin/bash
# Keystone on the Pi: Update firmware, started from the phone.
#
# Keystone saves the new package in ~/Documents/Keystone/Updates and writes request.json
# beside it; keystone-update.path notices and runs this as root.
#
# Two install slots, /opt/keystone/a and /opt/keystone/b, with /opt/keystone/current
# pointing at the one in use. The new version goes into the other slot, so the version
# running now is never touched. If the new one doesn't say it's running inside 60 seconds,
# current points back and the old version starts again.
#
# Everything it does goes in ~/Documents/Keystone/Updates/update.log.
set -u

ROOT="${KS_ROOT:-/opt/keystone}"
HOME_DIR="${KS_HOME:-/home/keystone/Documents/Keystone}"
SYSTEMCTL="${KS_SYSTEMCTL:-systemctl}"
WAIT="${KS_WAIT:-60}"
REPO="kevinwkaye/keystone"
UPDATES="$HOME_DIR/Updates"
REQUEST="$UPDATES/request.json"
LOG="$UPDATES/update.log"

log() { echo "$(date '+%Y-%m-%d %H:%M:%S')  $*" >> "$LOG"; }
field() { python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get(sys.argv[2], ""))' "$REQUEST" "$1" 2>/dev/null; }

[ -f "$REQUEST" ] || exit 0
VERSION="$(field version)"
FROM="$(field from)"
PACKAGE="$(field installer)"
rm -f "$REQUEST"
log "Update firmware: $FROM to $VERSION"

# ---- 1. is it a Keystone release?
NAME="$(basename "$PACKAGE")"
case "$(realpath -m "$PACKAGE")" in
    "$(realpath -m "$UPDATES")"/*) ;;
    *) log "Refused: $PACKAGE isn't in the Updates folder"; exit 1 ;;
esac
if ! [[ "$NAME" =~ ^Keystone-[0-9.]+-linux-arm64\.tar\.gz$ ]] || [ ! -f "$PACKAGE" ]; then
    log "Refused: $NAME isn't a Keystone package"; exit 1
fi
HAVE="$(sha256sum "$PACKAGE" | cut -d' ' -f1)"
if [ -z "${KS_OFFLINE:-}" ] && REL="$(curl -fsS --max-time 15 -H 'User-Agent: Keystone' "https://api.github.com/repos/$REPO/releases/tags/v$VERSION" 2>/dev/null)"; then
    WANT="$(printf '%s' "$REL" | python3 -c 'import json,sys; n=sys.argv[1]; a=[x for x in json.load(sys.stdin)["assets"] if x["name"]==n]; print(a[0].get("digest","none") if a else "missing")' "$NAME")"
    case "$WANT" in
        missing) log "Refused: GitHub has no $NAME in release v$VERSION"; rm -f "$PACKAGE"; exit 1 ;;
        none|"") ;;
        "sha256:$HAVE") log "Matches the release on GitHub" ;;
        *) log "Refused: $NAME doesn't match the one on GitHub"; rm -f "$PACKAGE"; exit 1 ;;
    esac
else
    log "GitHub not reachable, going on the phone's checksum ($HAVE)"
fi

# ---- 2. unpack into the slot not in use
CUR="$(basename "$(readlink -f "$ROOT/current" 2>/dev/null || echo "$ROOT/a")")"
[ "$CUR" = "a" ] && NEXT="b" || NEXT="a"
TMP="$(mktemp -d)"
if ! tar -xzf "$PACKAGE" -C "$TMP" || [ ! -f "$TMP/keystone/keystone" ]; then
    log "Refused: the package didn't unpack"; rm -rf "$TMP"; exit 1
fi
rm -rf "${ROOT:?}/$NEXT"
mv "$TMP/keystone" "$ROOT/$NEXT"
rm -rf "$TMP"
chmod 755 "$ROOT/$NEXT/keystone"
log "Unpacked $VERSION into slot $NEXT (slot $CUR keeps $FROM)"

started_since() {   # version, since
    python3 - "$HOME_DIR/started.json" "$1" "$2" <<'EOF' 2>/dev/null
import json, sys
s = json.load(open(sys.argv[1]))
sys.exit(0 if s.get("version") == sys.argv[2] and int(s.get("at", 0)) >= int(sys.argv[3]) else 1)
EOF
}
switch_to() { ln -sfn "$ROOT/$1" "$ROOT/current.new" && mv -T "$ROOT/current.new" "$ROOT/current"; }
wait_for() {   # version, since
    for _ in $(seq 1 "$WAIT"); do
        started_since "$1" "$2" && return 0
        sleep 1
    done
    return 1
}

# ---- 3. switch over and start
SINCE="$(date +%s)"
switch_to "$NEXT"
$SYSTEMCTL restart keystone
if wait_for "$VERSION" "$SINCE"; then
    log "Keystone $VERSION is running from slot $NEXT"
    # the Pi's helpers from the new version (the Wi-Fi network arrived in 0.4.8)
    if [ -f "$ROOT/$NEXT/pi/keystone-wifi.sh" ]; then
        install -m 755 "$ROOT/$NEXT/pi/keystone-wifi.sh" "$ROOT/keystone-wifi.sh"
        install -m 644 "$ROOT/$NEXT/pi/keystone-wifi.path" /etc/systemd/system/keystone-wifi.path
        install -m 644 "$ROOT/$NEXT/pi/keystone-wifi.service" /etc/systemd/system/keystone-wifi.service
        $SYSTEMCTL daemon-reload; $SYSTEMCTL enable --now keystone-wifi.path >/dev/null 2>&1
    fi
    rm -f "$PACKAGE"
    exit 0
fi

# ---- 4. it didn't start: back to the old slot
log "Keystone $VERSION didn't start inside $WAIT seconds, going back to $FROM"
SINCE="$(date +%s)"
switch_to "$CUR"
$SYSTEMCTL restart keystone
if wait_for "$FROM" "$SINCE"; then log "Back on $FROM"; else log "$FROM didn't start either: check journalctl -u keystone"; fi
exit 1
