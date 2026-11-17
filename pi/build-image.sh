#!/bin/bash
# Builds the ready-made Keystone image for the Raspberry Pi: the official Raspberry Pi OS Lite
# (64-bit) with Keystone already installed, so the Pi needs no command line at all.
#
#   sudo pi/build-image.sh <Keystone-x.y.z-linux-arm64.tar.gz> <out.img.xz>
#
# Runs on an ARM64 Linux machine (the build machine), so the image's own programs run
# directly inside it with no emulation. Raspberry Pi Imager's settings (Wi-Fi, your sign-in,
# SSH) still work on the result, the same as on the plain Raspberry Pi OS.
set -euo pipefail

PACKAGE="$(realpath "${1:?the Keystone linux-arm64 package}")"
OUT="$(realpath -m "${2:?where to write the image}")"
BASE_URL="${KS_PI_OS_URL:-https://downloads.raspberrypi.com/raspios_lite_arm64_latest}"
WORK="$(mktemp -d)"
MNT="$WORK/root"
LOOP=""

cleanup() {
    set +e
    for m in dev/pts dev proc sys boot/firmware ""; do mountpoint -q "$MNT/$m" && umount -l "$MNT/$m"; done
    [ -n "$LOOP" ] && losetup -d "$LOOP"
}
trap cleanup EXIT

[ "$(id -u)" = 0 ] || { echo "Run it with sudo"; exit 1; }
[ "$(uname -m)" = "aarch64" ] || { echo "Build it on ARM64 Linux"; exit 1; }

echo "== Raspberry Pi OS Lite (64-bit)"
curl -fsSL "$BASE_URL" -o "$WORK/base.img.xz"
xz -d -T0 "$WORK/base.img.xz"
IMG="$WORK/base.img"

echo "== Room for Keystone and its libraries"
truncate -s +768M "$IMG"
parted -s "$IMG" resizepart 2 100%
LOOP="$(losetup -fP --show "$IMG")"
e2fsck -fy "${LOOP}p2" >/dev/null || true
resize2fs "${LOOP}p2" >/dev/null

echo "== Mounting"
mkdir -p "$MNT"
mount "${LOOP}p2" "$MNT"
mkdir -p "$MNT/boot/firmware"
mount "${LOOP}p1" "$MNT/boot/firmware"
mount --bind /dev "$MNT/dev"
mount --bind /dev/pts "$MNT/dev/pts"
mount -t proc proc "$MNT/proc"
mount -t sysfs sys "$MNT/sys"
cp /etc/resolv.conf "$MNT/etc/resolv.conf.keystone"
mv "$MNT/etc/resolv.conf" "$MNT/etc/resolv.conf.pi" 2>/dev/null || true
cp /etc/resolv.conf "$MNT/etc/resolv.conf"

echo "== Keystone inside"
mkdir -p "$MNT/tmp/keystone-pkg"
tar -xzf "$PACKAGE" -C "$MNT/tmp/keystone-pkg"
# no services may start inside the build
printf '#!/bin/sh\nexit 101\n' > "$MNT/usr/sbin/policy-rc.d"
chmod 755 "$MNT/usr/sbin/policy-rc.d"
chroot "$MNT" /bin/bash /tmp/keystone-pkg/keystone/pi/deps.sh
chroot "$MNT" /bin/bash /tmp/keystone-pkg/keystone/pi/install.sh /tmp/keystone-pkg/keystone
chroot "$MNT" apt-get clean
rm -rf "$MNT/tmp/keystone-pkg" "$MNT/usr/sbin/policy-rc.d" "$MNT/etc/resolv.conf.keystone"
rm -f "$MNT/etc/resolv.conf"
mv "$MNT/etc/resolv.conf.pi" "$MNT/etc/resolv.conf" 2>/dev/null || true

# check what went in
test -x "$MNT/opt/keystone/current/keystone"
test -L "$MNT/etc/systemd/system/multi-user.target.wants/keystone.service"
test -L "$MNT/etc/systemd/system/multi-user.target.wants/keystone-update.path"
echo "Keystone $(cat "$MNT/opt/keystone/current/VERSION") is in the image and starts at boot"

echo "== Packing"
sync
cleanup
LOOP=""
trap - EXIT
xz -T0 -6 -c "$IMG" > "$OUT"
rm -rf "$WORK"
ls -lh "$OUT"
