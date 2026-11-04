#!/bin/bash
# Packs a flashable boot image from a kernel release and YOUR phone's vendor_boot.img:
#
#   scripts/op9-boot-image.sh <vendor_boot.img> <Image> <initramfs.gz> <output.img>
#
# The bootloader only accepts a boot image whose DTB section is the stock vendor_boot
# DTB blob, which it applies its DTBO to. The kernel ignores that blob and boots its
# built-in device tree. The blob is OnePlus's, so releases don't carry it: take it from
# the vendor_boot.img of the firmware on your phone (see docs/INSTALL.md).
# Needs unpack_bootimg and mkbootimg (Android's mkbootimg package). CMDLINE= is passed
# on to mkboot.sh.
set -euo pipefail
HERE=$(dirname "$(realpath "$0")")
[ $# -eq 4 ] || { echo "usage: $0 <vendor_boot.img> <Image> <initramfs.gz> <output.img>" >&2; exit 1; }
for f in "$1" "$2" "$3"; do [ -s "$f" ] || { echo "$f: missing or empty" >&2; exit 1; }; done

TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
unpack_bootimg --boot_img "$1" --out "$TMP" >/dev/null
[ -s "$TMP/dtb" ] || { echo "$1: no dtb section; is it vendor_boot.img?" >&2; exit 1; }
"$HERE/mkboot.sh" "$2" "$TMP/dtb" "$3" "$4"
