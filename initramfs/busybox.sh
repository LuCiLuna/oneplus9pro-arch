#!/bin/bash
# Builds the static aarch64 busybox the initramfs needs: initramfs/busybox.sh <output>
#
# Any static aarch64 busybox with the telnetd and udhcpd applets does; this builds one
# from source with the cross compiler, since distribution builds often leave them out
# (Alpine's busybox-static, for instance). Upstream's defconfig has everything init uses:
# ip, telnetd with -F, udhcpd, switch_root, mknod and the installer.
set -euo pipefail
VERSION=${BUSYBOX_VERSION:-1.37.0}
OUTPUT=$(realpath -m "$1")
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT

curl -fsSL -o "$WORK/busybox.tar.bz2" "https://busybox.net/downloads/busybox-$VERSION.tar.bz2"
tar -xjf "$WORK/busybox.tar.bz2" -C "$WORK"
cd "$WORK/busybox-$VERSION"
make -s ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- defconfig >/dev/null
# Static; no tc, whose build breaks against current kernel headers and which nothing here uses;
# and no SHA hardware acceleration, which defconfig turns on but only exists for x86
sed -i -e 's/^# CONFIG_STATIC is not set/CONFIG_STATIC=y/' -e 's/^CONFIG_TC=y/# CONFIG_TC is not set/' \
	-e 's/^CONFIG_SHA1_HWACCEL=y/# CONFIG_SHA1_HWACCEL is not set/' \
	-e 's/^CONFIG_SHA256_HWACCEL=y/# CONFIG_SHA256_HWACCEL is not set/' .config
# BusyBox has no olddefconfig: take the defaults for anything the edits above uncovered
make -s ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- oldconfig < <(yes "") >/dev/null
make -s ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j"$(nproc)" busybox >/dev/null
# The build machine can't run an aarch64 binary, so check the config: the applets init uses,
# and telnetd's standalone mode, which gives it -F and -b
for opt in TELNETD FEATURE_TELNETD_STANDALONE UDHCPD SWITCH_ROOT IP MKNOD STATIC; do
	grep -qx "CONFIG_$opt=y" .config || { echo "busybox: CONFIG_$opt is off" >&2; exit 1; }
done
install -m 755 busybox "$OUTPUT"
ls -lh "$OUTPUT"
