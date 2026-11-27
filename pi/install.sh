#!/bin/bash
# Keystone on the Pi: put an unpacked Keystone package in place and set the Pi up to run it.
# Used by setup.sh on a running Pi and by build-image.sh inside the ready-made image, so both
# end up the same. Needs root. Doesn't start anything: the caller does, or the Pi's next boot.
#
#   install.sh <unpacked package folder, the one holding keystone and pi/>
set -euo pipefail

PKG="${1:?the unpacked Keystone package}"
ROOT=/opt/keystone
USER_NAME=keystone

[ -f "$PKG/keystone" ] && [ -d "$PKG/pi" ] || { echo "$PKG isn't a Keystone package"; exit 1; }

# the user Keystone runs as (separate from whoever you sign in as)
id "$USER_NAME" >/dev/null 2>&1 || useradd -m -s /bin/bash "$USER_NAME"
for g in audio plugdev gpio systemd-journal; do getent group "$g" >/dev/null && usermod -aG "$g" "$USER_NAME"; done
install -d -o "$USER_NAME" -g "$USER_NAME" "/home/$USER_NAME/Documents" "/home/$USER_NAME/Documents/Keystone" "/home/$USER_NAME/Documents/Keystone/Updates"

# Keystone in slot a, current pointing at it
mkdir -p "$ROOT"
rm -rf "$ROOT/a"
cp -a "$PKG" "$ROOT/a"
chmod 755 "$ROOT/a/keystone"
ln -sfn "$ROOT/a" "$ROOT/current"

# start at boot, and Update firmware
install -m 755 "$PKG/pi/keystone-update.sh" "$ROOT/keystone-update.sh"
install -m 644 "$PKG/pi/keystone.service" /etc/systemd/system/keystone.service
install -m 644 "$PKG/pi/keystone-update.path" /etc/systemd/system/keystone-update.path
install -m 644 "$PKG/pi/keystone-update.service" /etc/systemd/system/keystone-update.service

# Keystone's own Wi-Fi network for the phone
install -m 755 "$PKG/pi/keystone-wifi.sh" "$ROOT/keystone-wifi.sh"
install -m 644 "$PKG/pi/keystone-wifi.path" /etc/systemd/system/keystone-wifi.path
install -m 644 "$PKG/pi/keystone-wifi.service" /etc/systemd/system/keystone-wifi.service

# the service button on the back: hold 3 s and let go to shut down (0.6.2)
install -m 755 "$PKG/pi/keystone-power.sh" "$ROOT/keystone-power.sh"
install -m 644 "$PKG/pi/keystone-power.path" /etc/systemd/system/keystone-power.path
install -m 644 "$PKG/pi/keystone-power.service" /etc/systemd/system/keystone-power.service

# real-time audio, the processor at full speed, Wi-Fi at full power
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
mkdir -p /etc/NetworkManager/conf.d
printf '[connection]\nwifi.powersave = 2\n' > /etc/NetworkManager/conf.d/keystone-wifi.conf

# enable works without a running systemd (inside the image build too)
systemctl enable keystone-performance.service keystone-update.path keystone-wifi.path keystone-power.path keystone.service
echo "Keystone $(cat "$ROOT/a/VERSION" 2>/dev/null || echo) installed in $ROOT/a"
