#!/bin/bash
# Builds the OnePlus 9 Pro kernel: Linux v7.3 plus kernel/patches.
#
#   kernel/build.sh [workdir]        default workdir: ./work
#
# WIFI_MAC="aa bb cc dd ee ff"   this phone's Wi-Fi address, see docs/INSTALL.md
# BT_ADDR="aa bb cc dd ee ff"    this phone's Bluetooth address, in the normal order
# KERNEL_GIT=<url>               where to fetch v7.3 from
# JOBS=<n>                       parallel jobs
set -euo pipefail
HERE=$(dirname "$(realpath "$0")")
WORK=$(realpath -m "${1:-work}")
KERNEL_GIT=${KERNEL_GIT:-https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git}
SRC=$WORK/linux; OUT=$WORK/build; JOBS=${JOBS:-$(nproc)}
MK=(make -C "$SRC" O="$OUT" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j"$JOBS")

mkdir -p "$WORK" "$OUT"
if [ ! -d "$SRC" ]; then
	git clone --depth 1 --branch v7.3 "$KERNEL_GIT" "$SRC"
fi

# The series is applied once per set of patches. The marker is only written
# after git am succeeds, so an interrupted or failed run is redone from v7.3
# instead of silently building whatever was applied; a changed series is
# reapplied the same way.
WANT=$(cat "$HERE"/patches/*.patch | sha256sum | cut -d' ' -f1)
if [ "$WANT" != "$(cat "$WORK/patches.sha256" 2>/dev/null || true)" ]; then
	rm -f "$WORK/patches.sha256"
	git -C "$SRC" am --abort 2>/dev/null || true
	git -C "$SRC" reset -q --hard v7.3
	git -C "$SRC" -c user.name=op9-build -c user.email=op9-build@localhost am -q "$HERE"/patches/*.patch
	echo "$WANT" > "$WORK/patches.sha256"
fi

if [ -n "${WIFI_MAC:-}" ]; then
	sed -i "s/local-mac-address = \[[0-9a-f ]*\];/local-mac-address = [$WIFI_MAC];/" \
		"$SRC/arch/arm64/boot/dts/qcom/sm8350-oneplus-lemonadep.dts"
fi

if [ -n "${BT_ADDR:-}" ]; then
	# Device trees store this address in reverse
	BT_LE=$(echo "$BT_ADDR" | tr ' ' '\n' | tac | tr '\n' ' ' | sed 's/ $//')
	sed -i "s/local-bd-address = \[[0-9a-f ]*\];/local-bd-address = [$BT_LE];/" \
		"$SRC/arch/arm64/boot/dts/qcom/sm8350-oneplus-lemonadep.dts"
fi

cp "$HERE/op9.config" "$OUT/.config"
"$SRC/scripts/config" --file "$OUT/.config" --set-str ARM64_OP9_BUILTIN_DTB_PATH "$OUT/op9-builtin.dtb"
"${MK[@]}" olddefconfig

# The kernel carries its own device tree, so build that first
"${MK[@]}" qcom/sm8350-oneplus-lemonadep.dtb
cp "$OUT/arch/arm64/boot/dts/qcom/sm8350-oneplus-lemonadep.dtb" "$OUT/op9-builtin.dtb"
"${MK[@]}" Image modules
rm -rf "$WORK/modroot"
"${MK[@]}" modules_install INSTALL_MOD_PATH="$WORK/modroot" INSTALL_MOD_STRIP=1

echo "Image:   $OUT/arch/arm64/boot/Image"
echo "Modules: $WORK/modroot"
