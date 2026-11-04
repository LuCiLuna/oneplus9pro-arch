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
make -s ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- defconfig
# Static, and no tc: its build breaks against current kernel headers and nothing here uses it
sed -i -e 's/^# CONFIG_STATIC is not set/CONFIG_STATIC=y/' -e 's/^CONFIG_TC=y/# CONFIG_TC is not set/' .config
make -s ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- olddefconfig >/dev/null
make -s ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j"$(nproc)" busybox >/dev/null
for applet in telnetd udhcpd switch_root ip mknod; do
	./busybox --list | grep -qx "$applet" || { echo "busybox: no $applet applet" >&2; exit 1; }
done
install -m 755 busybox "$OUTPUT"
ls -lh "$OUTPUT"
