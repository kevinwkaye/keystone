#!/bin/bash
# Keystone on a Raspberry Pi: run once on a fresh Raspberry Pi OS Lite (64-bit).
#
#   curl -fsSL https://raw.githubusercontent.com/kevinwkaye/keystone/main/pi/setup.sh | sudo bash
#
# Easier: write the ready-made image (Keystone-<version>-pi.img.xz, on each release) with
# Raspberry Pi Imager instead, and skip this. This script is for a Pi that's already set up.
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
USER_NAME=keystone

[ "$(id -u)" = 0 ] || { echo "Run it with sudo"; exit 1; }
[ "$(uname -m)" = "aarch64" ] || { echo "This needs the 64-bit Raspberry Pi OS"; exit 1; }
say() { echo; echo "== $*"; }

say "The latest Keystone"
apt-get update -q && apt-get install -y -q curl python3 >/dev/null
URL="$(curl -fsS -H 'User-Agent: Keystone' "https://api.github.com/repos/$REPO/releases/latest" \
      | python3 -c 'import json,sys; print([a["browser_download_url"] for a in json.load(sys.stdin)["assets"] if a["name"].endswith("-linux-arm64.tar.gz")][0])')"
echo "$URL"
TMP="$(mktemp -d)"
curl -fsSL "$URL" -o "$TMP/keystone.tar.gz"
tar -xzf "$TMP/keystone.tar.gz" -C "$TMP"

say "Libraries Keystone needs"
bash "$TMP/keystone/pi/deps.sh"

say "Installing, with start at boot and Update firmware"
bash "$TMP/keystone/pi/install.sh" "$TMP/keystone"
rm -rf "$TMP"
systemctl daemon-reload
systemctl start keystone-performance.service keystone-update.path keystone-wifi.path keystone-power.path keystone.service

say "Done"
echo "Keystone $(basename "$URL" | sed 's/Keystone-\(.*\)-linux.*/\1/') is running. The phone finds it on the hotspot like the laptop."
echo "Log: journalctl -u keystone   Updates: /home/$USER_NAME/Documents/Keystone/Updates/update.log"
