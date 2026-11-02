#!/bin/bash
# Builds the initramfs: initramfs/build.sh <static aarch64 busybox> <output.gz>
#
# The busybox needs the telnetd and udhcpd applets. It brings up USB networking
# and an unauthenticated root shell on the USB link, then switches to the root
# filesystem on userdata. See docs/INSTALL.md.
set -euo pipefail
HERE=$(dirname "$(realpath "$0")")
BUSYBOX=$(realpath "$1"); OUTPUT=$(realpath -m "$2")
ROOT=$(mktemp -d); trap 'rm -rf "$ROOT"' EXIT

mkdir -p "$ROOT"/{bin,sbin,usr/bin,usr/sbin,dev,etc,mnt,proc,run,sys,tmp,config}
cp "$BUSYBOX" "$ROOT/bin/busybox"
aarch64-linux-gnu-gcc -static -O2 -o "$ROOT/bin/dmcreate" "$HERE/../tools/dmcreate.c"
install -m 755 "$HERE/init" "$ROOT/init"
(cd "$ROOT" && find . | cpio -o -H newc --quiet --owner 0:0 | gzip -9) > "$OUTPUT"
ls -lh "$OUTPUT"
