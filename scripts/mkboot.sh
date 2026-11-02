#!/bin/bash
# Packs a flashable boot image: scripts/mkboot.sh <Image> <carrier.dtb> <initramfs.gz> <output.img>
#
# The bootloader only accepts an image with the stock device trees attached, so
# carrier.dtb comes from the stock vendor_boot. The kernel then ignores them and
# uses its built-in device tree (CONFIG_ARM64_OP9_BUILTIN_DTB).
set -euo pipefail
CMDLINE=${CMDLINE:-"fw_devlink=permissive deferred_probe_timeout=30 fw_devlink.sync_state=timeout mem_sleep_default=s2idle console=tty0"}
mkbootimg --header_version 2 \
	--kernel "$1" --dtb "$2" --ramdisk "$3" \
	--cmdline "$CMDLINE" \
	--base 0x0 --kernel_offset 0x8000 --ramdisk_offset 0x1000000 \
	--tags_offset 0x100 --dtb_offset 0x1f00000 --pagesize 4096 \
	--os_version 17.0.0 --os_patch_level 2026-10 \
	-o "$4"
ls -lh "$4"
