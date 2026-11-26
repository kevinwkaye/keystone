#!/bin/bash
# Keystone on the Pi: the libraries it needs, on Raspberry Pi OS Lite (64-bit).
# Package names changed between releases (the t64 ones are Trixie), so each has a fallback.
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get update -q
pkg() { for p in "$@"; do apt-get install -y -q --no-install-recommends "$p" >/dev/null 2>&1 && return 0; done; echo "Couldn't install any of: $*"; return 1; }
pkg libasound2t64 libasound2
pkg libcurl4t64 libcurl4
pkg libfreetype6
pkg libfontconfig1
for p in libx11-6 libxrandr2 libxinerama1 libxcursor1 libxext6 libgl1 python3 curl; do pkg "$p"; done
# the projector's video player (0.6.0); build-image.sh adds it itself, to keep its packages
[ -n "${KS_NO_MPV:-}" ] || pkg mpv
