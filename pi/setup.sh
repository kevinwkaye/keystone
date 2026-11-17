#!/bin/bash
# Keystone on a Raspberry Pi: run once on a fresh Raspberry Pi OS Lite (64-bit).
#
#   curl -fsSL https://raw.githubusercontent.com/kevinwkaye/keystone/main/pi/setup.sh | sudo bash
#
# Before that, in Raspberry Pi Imager: Raspberry Pi OS Lite (64-bit), hostname "keystone",
# user "keystone", your phone's hotspot as the Wi-Fi, and SSH on. The Pi needs the internet
# once, through the hotspot, to fetch Keystone.
#
# It installs the latest Keystone release, starts it at every boot, gives audio real-time
# priority, keeps the processor at full speed and the Wi-Fi at full power, and sets up
# Update firmware so every update after this one comes from the phone.
set -euo pipefail

REPO="kevinwkaye/keystone"
ROOT=/opt/keystone
USER_NAME=keystone

[ "$(id -u)" = 0 ] || { echo "Run it with sudo"; exit 1; }
[ "$(uname -m)" = "aarch64" ] || { echo "This needs the 64-bit Raspberry Pi OS"; exit 1; }
say() { echo; echo "== $*"; }

say "Libraries Keystone needs"
apt-get update -q
pkg() { for p in "$@"; do apt-get install -y -q "$p" >/dev/null 2>&1 && return 0; done; echo "Couldn't install any of: $*"; return 1; }
pkg libasound2t64 libasound2
pkg libcurl4t64 libcurl4
pkg libfreetype6
pkg libfontconfig1
for p in libx11-6 libxrandr2 libxinerama1 libxcursor1 libxext6 libgl1 python3 curl; do pkg "$p"; done

say "The keystone user"
id "$USER_NAME" >/dev/null 2>&1 || useradd -m -s /bin/bash "$USER_NAME"
usermod -aG audio "$USER_NAME"
sudo -u "$USER_NAME" mkdir -p "/home/$USER_NAME/Documents/Keystone/Updates"

say "The latest Keystone"
URL="$(curl -fsS -H 'User-Agent: Keystone' "https://api.github.com/repos/$REPO/releases/latest" \
      | python3 -c 'import json,sys; print([a["browser_download_url"] for a in json.load(sys.stdin)["assets"] if a["name"].endswith("-linux-arm64.tar.gz")][0])')"
echo "$URL"
TMP="$(mktemp -d)"
curl -fsSL "$URL" -o "$TMP/keystone.tar.gz"
tar -xzf "$TMP/keystone.tar.gz" -C "$TMP"
mkdir -p "$ROOT"
rm -rf "$ROOT/a"
mv "$TMP/keystone" "$ROOT/a"
rm -rf "$TMP"
chmod 755 "$ROOT/a/keystone"
ln -sfn "$ROOT/a" "$ROOT/current"

say "Starting at boot, and Update firmware"
install -m 755 "$ROOT/a/pi/keystone-update.sh" "$ROOT/keystone-update.sh"
install -m 644 "$ROOT/a/pi/keystone.service" /etc/systemd/system/keystone.service
install -m 644 "$ROOT/a/pi/keystone-update.path" /etc/systemd/system/keystone-update.path
install -m 644 "$ROOT/a/pi/keystone-update.service" /etc/systemd/system/keystone-update.service

say "Real-time audio, full speed, Wi-Fi at full power"
cat > /etc/security/limits.d/keystone.conf <<'EOF'
@audio - rtprio 95
@audio - memlock unlimited
EOF
cat > /etc/systemd/system/keystone-performance.service <<'EOF'
[Unit]
Description=Keystone: processor at full speed
[Service]
Type=oneshot
ExecStart=/bin/sh -c 'for g in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do echo performance > "$g"; done'
[Install]
WantedBy=multi-user.target
EOF
if [ -d /etc/NetworkManager/conf.d ]; then
    printf '[connection]\nwifi.powersave = 2\n' > /etc/NetworkManager/conf.d/keystone-wifi.conf
fi

systemctl daemon-reload
systemctl enable --now keystone-performance.service keystone-update.path keystone.service

say "Done"
echo "Keystone $(basename "$URL" | sed 's/Keystone-\(.*\)-linux.*/\1/') is running. The phone finds it on the hotspot like the laptop."
echo "Log: journalctl -u keystone   Updates: /home/$USER_NAME/Documents/Keystone/Updates/update.log"
