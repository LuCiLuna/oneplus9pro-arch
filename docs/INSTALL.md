# Install guide

> **This erases the phone and can brick it.** It has only been done on one T-Mobile LE2127, and
> never from scratch in this exact order, so treat it as a detailed map, not a tested script.
> Steps 5 and 6 are the least proven: that phone reached its current layout through a different
> route (see [How this phone got here](#how-this-phone-got-here)).

**Never write to** `xbl`, `persist`, `modem`, the EFS partitions or `param`.

## What you need

- A OnePlus 9 Pro with an **unlocked bootloader**, running LineageOS with root, so you can copy firmware
- The stock firmware level this was tested on: the bootloader from OxygenOS 13.1.0.593, as used by LineageOS 24
- A Linux PC with `aarch64-linux-gnu-gcc`, `fastboot`, `mkbootimg`, `unpack_bootimg`, `cpio`, `dtc` and the usual kernel build tools
- A static aarch64 `busybox` that includes the `telnetd` and `udhcpd` applets. Alpine's `busybox-static` doesn't have them.

## 1. Back up your phone

From rooted Android, copy **every** partition to your PC, or at the very least `persist`, `modem_a`,
`modem_b`, `modemst1`, `modemst2`, `fsg`, `fsc`, `boot_*`, `vendor_boot_*`, `dtbo_*` and `vbmeta_*`.
Keep these safe. `persist` and the EFS partitions can't be replaced.

## 2. Copy your files from Android

Android is about to be wiped, so copy these now, as root:

| From the phone | Used for |
|---|---|
| `/vendor/firmware/` | GPU, video and IPA firmware |
| `/vendor/firmware_mnt/image/` | DSP, modem and Wi-Fi firmware |
| `/odm/etc/wifi/bdwlan.elf`, `/odm/etc/wifi/regdb.bin` | Wi-Fi board data and regulatory database |
| `/mnt/vendor/persist/sensors/registry/registry/` | **Your phone's** sensor calibration |
| `/mnt/vendor/persist/wlan_mac.bin` | **Your phone's** Wi-Fi address |

Don't share the last two or post them publicly. Also keep `vendor_boot.img` from your LineageOS build.

Write down your **Bluetooth address** too. Android shows it under Settings → About phone. The chip
has no address of its own, and stock reads it from the modem, which isn't running here. On the one
phone tested, it was the Wi-Fi address minus 1, but that's a single example, so don't rely on it.

## 3. Build the kernel

`wlan_mac.bin` contains a line like `Intf0MacAddress=AABBCCDDEEFF`. Pass that address, in lowercase
pairs, so your phone uses its own. If you skip this, you get the firmware's placeholder address,
which every phone without this setting shares.

Do the same with your Bluetooth address, in the order Android shows it. Without it, BlueZ sees no
Bluetooth adapter at all.

```sh
WIFI_MAC="aa bb cc dd ee ff" BT_ADDR="aa bb cc dd ee ff" kernel/build.sh work
```

This produces `work/build/arch/arm64/boot/Image` and modules in `work/modroot`.

## 4. Build the boot image

The bootloader rejects any image without the stock device trees attached, so take them from `vendor_boot.img`:

```sh
unpack_bootimg --boot_img vendor_boot.img --out vb
initramfs/build.sh /path/to/static/busybox work/initramfs.gz
scripts/mkboot.sh work/build/arch/arm64/boot/Image vb/dtb work/initramfs.gz work/op9-boot.img
```

### Test it before flashing

```sh
fastboot boot work/op9-boot.img
```

This changes nothing on the phone. Send the command **once**. The bootloader takes a few seconds
to hand over, and repeating bootloader commands in a loop can change the active slot. The screen
may stay black with `fastboot boot`, which doesn't prove anything. Look for USB networking instead:

```sh
sudo ip addr add 172.16.42.2/24 dev <usb interface>
telnet 172.16.42.1
```

That shell is an **unauthenticated root shell** over USB. It's only there while the phone is in
the initramfs. If it doesn't appear, your model may need different device tree IDs. Please open an issue.

## 5. Create the root filesystem

The initramfs looks for an ext4 filesystem on the `userdata` partition. Build one on your PC:

```sh
truncate -s 8G root.img
mkfs.ext4 -L op9root root.img
sudo mount root.img /mnt/op9
sudo bsdtar -xpf ArchLinuxARM-aarch64-latest.tar.gz -C /mnt/op9
sudo cp -a rootfs/. /mnt/op9/
sudo cp -a work/modroot/lib/modules /mnt/op9/usr/lib/
```

### Firmware

Rebuild the split images and place them where the kernel expects them:

```sh
FW=/mnt/op9/usr/lib/firmware/qcom/sm8350/OnePlus/lemonade
sudo mkdir -p $FW
for n in adsp cdsp slpi modem; do
    sudo tools/pil-squash.py $FW/$n.mbn image/$n.mdt
done
for n in a660_zap ipa_fws vpu20_4v; do
    sudo tools/pil-squash.py $FW/$n.mbn firmware/$n.mdt
done
sudo cp image/*.jsn firmware/a660_sqe.fw firmware/a660_gmu.bin $FW/
sudo cp -r image/modem_pr $FW/     # carrier configs the modem reads at boot
sudo ln -s sm8350/OnePlus/lemonade/a660_sqe.fw /mnt/op9/usr/lib/firmware/qcom/a660_sqe.fw
sudo ln -s sm8350/OnePlus/lemonade/a660_gmu.bin /mnt/op9/usr/lib/firmware/qcom/a660_gmu.bin

for d in WCN6855/hw2.0 WCN6855/hw2.1; do
    W=/mnt/op9/usr/lib/firmware/updates/ath11k/$d
    sudo mkdir -p $W
    sudo cp image/amss.bin image/m3.bin regdb.bin $W/
    sudo cp bdwlan.elf $W/board.bin
done
```

Bluetooth firmware comes from Arch's `linux-firmware-atheros` package.

### Sensor calibration

```sh
Q=/mnt/op9/usr/share/qcom/sm8350/OnePlus/lemonade/sensors/registry
sudo mkdir -p $Q
sudo cp -r <your persist copy>/registry $Q/registry
echo "version=4" | sudo tee $Q/sns_reg_version
```

## 6. Flash

```sh
sudo umount /mnt/op9
img2simg root.img root.simg
adb reboot bootloader
fastboot flash userdata root.simg     # ERASES ALL ANDROID DATA
fastboot flash boot_a work/op9-boot.img
fastboot set_active a
fastboot reboot
```

The "orange state" warning at boot is normal. Leave `dtbo_a` and `vbmeta_a` stock. The filesystem
is only 8 GB at this point, so grow it once booted with `resize2fs /dev/op9/userdata`.

To reach the bootloader again from Linux, run `systemctl reboot --reboot-argument=bootloader`.

## 7. Set up Arch

Share your PC's internet over USB with `scripts/op9-nat.sh`, then log in from your PC. Arch Linux
ARM's defaults are `alarm/alarm` and `root/root`. **Change both, especially before using Wi-Fi.**
Phosh runs as the account with UID 1000, which is `alarm`. To use another account, change
`User=` in `/etc/systemd/system/phosh.service`.

```sh
ssh alarm@172.16.42.1
```

On the phone:

```sh
pacman-key --init && pacman-key --populate archlinuxarm
pacman -Syu
pacman -S --needed phosh phoc phosh-mobile-settings squeekboard gnome-console gnome-control-center \
    gnome-keyring networkmanager mesa vulkan-freedreno pipewire-libcamera libcamera libcamera-ipa \
    libcamera-tools snapshot firefox iw alsa-utils linux-firmware-atheros bluez bluez-utils feedbackd \
    libnotify swh-plugins
systemctl enable phosh NetworkManager bluetooth touchboost rtc-offset rtc-offset-save.timer op9-boot-successful
systemctl set-default graphical.target
```

`tools/` has source for `touchboost`, `op9-slot` and `alert-slider`, which those services run.
Cross-compile them statically and put them in `/usr/local/bin`:

```sh
aarch64-linux-gnu-gcc -static -O2 -o touchboost tools/touchboost.c
aarch64-linux-gnu-gcc -static -O2 -o op9-slot tools/op9-slot.c
aarch64-linux-gnu-gcc -static -O2 -o alert-slider tools/alert-slider.c
```

**Don't skip `op9-slot`.** On every boot, `op9-boot-successful` uses it to tell the bootloader
the slot booted fine. Without it, the bootloader counts down that slot's retries. After about
seven boots it marks the slot unbootable and switches to the other one, which looks like a brick.
If `op9-slot` is missing, the service fails and logs an error to the kernel log. Check with:

```sh
sudo op9-slot show     # the slot you booted should say successful=1
```

Then, logged in as `alarm`, turn on the alert slider and the charging notification. The `gsettings`
lines stop that notification popping up on the home screen, and let it light the screen when
charging starts:

```sh
systemctl --user enable --now op9-alert-slider op9-charge-status
app=org.gnome.desktop.notifications.application:/org/gnome/desktop/notifications/application/op9-charge-status/
gsettings set $app application-id op9-charge-status.desktop
gsettings set $app show-banners false
gsettings set org.gnome.desktop.notifications application-children "['op9-charge-status']"
gsettings set sm.puri.phosh.notifications wakeup-screen-triggers "['urgency','category']"
gsettings set sm.puri.phosh.notifications wakeup-screen-categories "['device.added']"
```

## 8. Patched packages

Five programs need patches. Each folder in `userspace/` has a `PKGBUILD` that builds a package
named `<name>-op9`, which replaces the stock Arch package. Build them on the phone as `alarm`:

```sh
sudo pacman -S --needed base-devel git
cp -r userspace ~/op9-pkgs
for d in hexagonrpc iio-sensor-proxy wlroots phoc libcamera; do
    (cd ~/op9-pkgs/$d && makepkg -si --nocheck) || break
done
```

`makepkg -si` asks before replacing each stock package. Answer yes. The whole set takes about
5 minutes on the phone. Pacman won't replace an `-op9` package with the stock one on updates. If
a later Arch update needs a newer version, the update stops with a dependency error instead.
Then update that folder's `pkgver` and patch.

| Folder | Package | Base version | Why it's needed |
|---|---|---|---|
| `userspace/wlroots` | `wlroots0.20-op9` | 0.20.2 | Without it, wlroots rejects Phosh's home bar and Phosh crashes |
| `userspace/phoc` | `phoc-op9` | 0.57.0 | Layout stalls, and display modes other than the preferred one |
| `userspace/libcamera` | `libcamera-op9` | 0.7.2 | Correct white balance, sensor support and autofocus. Replaces `libcamera` and `libcamera-ipa` |
| `userspace/hexagonrpc` | `hexagonrpc-op9` | [linux-msm/hexagonrpc](https://github.com/linux-msm/hexagonrpc) `da7a374` | Sensors: lets the DSP read and write its registry. Also creates the `fastrpc` user |
| `userspace/iio-sensor-proxy` | `iio-sensor-proxy-op9` | 3.9 | Auto-rotate |

After `hexagonrpc-op9` is installed, give the sensor registry to its user and start the sensors:

```sh
sudo chown -R fastrpc:fastrpc /usr/share/qcom/sm8350/OnePlus/lemonade/sensors/registry
sudo systemctl enable --now hexagonrpcd-sdsp
sudo systemctl restart iio-sensor-proxy
```

## 9. Firefox hardware video decoding

Firefox needs a private patched FFmpeg library for this:

```sh
firefox/build.sh       # Cross-builds FFmpeg 9.0.2 with the patch
```

On the phone, install these in `/opt/firefox-ffmpeg/lib/`:

- `libavcodec.so.63` from the build
- `libnoffvpx.so`, built with `aarch64-linux-gnu-gcc -shared -fPIC -O2 -o libnoffvpx.so firefox/noffvpx.c`

Then put `firefox/firefox.desktop` in `~/.local/share/applications/` and `firefox/user.js` in your
Firefox profile. The library must match Arch's FFmpeg version, so a major FFmpeg update can drop
Firefox back to software decoding until you rebuild it.

## 10. Firefox phone layout

[`mobile-config-firefox`](https://gitlab.postmarketos.org/postmarketOS/mobile-config-firefox) from
postmarketOS gives Firefox a phone interface and asks sites for their mobile versions. It isn't in
the Arch Linux ARM repos, so take the files from Alpine's package. Check
[Alpine's package page](https://pkgs.alpinelinux.org/package/edge/community/aarch64/mobile-config-firefox)
for the current version; this was tested with 5.5.2 on Firefox 159.

```sh
curl -LO https://dl-cdn.alpinelinux.org/alpine/edge/community/aarch64/mobile-config-firefox-5.5.2-r0.apk
mkdir mcf && tar -xzf mobile-config-firefox-5.5.2-r0.apk -C mcf
```

On the phone, copy these from `mcf/usr/lib/` into the same place under `/usr/lib/`:

- `mobile-config-firefox/`
- `firefox/mobile-config-autoconfig.js`
- `firefox/defaults/pref/mobile-config-prefs.js`

The package also has `firefox/distribution/policies.json`, which is optional and was left out here.
It changes the default search engine to DuckDuckGo, removes other search engines and installs
uBlock Origin. To undo the layout, delete the three items above and restart Firefox.

## 11. Android apps (Waydroid)

The kernel config already has what Waydroid needs: binder, PSI, nftables NAT and Android's policy
routing and packet filters. Without the policy routing, Android has no internet.

```sh
pacman -S --needed waydroid dnsmasq wtype
waydroid init -s GAPPS        # or leave out -s GAPPS for no Google apps; downloads about 1.5 GB
systemctl enable --now waydroid-container
```

Then open **Waydroid** from the app grid as `alarm`. Android 13 takes about 20 seconds to start.
If its sound is too quiet, raise the volume inside Android's settings.

For Google Play, get the device ID and register it at
<https://www.google.com/android/uncertified>, wait a few minutes, then restart Android:

```sh
waydroid shell -- sqlite3 /data/data/com.google.android.gsf/databases/gservices.db \
    "select value from main where name = 'android_id';"
```

`rootfs/` has a **Stop Android** launcher (`/usr/local/bin/waydroid-stop`). Android keeps using
battery while it runs, even with its window closed.

## 12. No app loading splash

Phosh shows a splash until an app's window appears, which often outlasts the app's own startup
here. `op9-no-splash` writes copies of the launchers that skip it into
`~/.local/share/applications`, and the pacman hook in `rootfs/` reruns it after installs. Run it
once as `alarm`, and `op9-no-splash --undo` to remove the copies.

## 13. Modem

The modem needs three services that Arch Linux ARM doesn't package, and a ModemManager with two
upstream crash fixes. Build them on the phone like the packages in step 8, `qrtr` first:

```sh
sudo pacman -S --needed vala gobject-introspection bash-completion
for d in qrtr rmtfs tqftpserv modemmanager; do
    (cd ~/op9-pkgs/$d && makepkg -si) || break
done
sudo systemctl enable --now tqftpserv op9-sim-provision ModemManager
sudo systemctl start rmtfs     # starts by itself on later boots
```

The modem firmware doesn't open a session for the SIM by itself (on Android the radio layer
does), so `op9-sim-provision` does it at boot, before ModemManager starts. A SIM inserted while
the phone is running needs `sudo systemctl restart op9-sim-provision ModemManager`.

Add mobile data with your carrier's APN, for example AT&T's `nrphone`:

```sh
sudo nmcli connection add type gsm ifname '*' con-name 'Mobile data' apn nrphone
```

`rmtfs` serves the modem's storage from the `modemst1`, `modemst2`, `fsg` and `fsc` partitions.
The packaged service runs it with `-r`, so the modem's writes stay in memory and those partitions
are only read. `rmtfs` then starts the modem. Check it with:

```sh
cat /sys/class/remoteproc/remoteproc1/state    # running
mmcli -L
```

## How this phone got here

The phone first ran Linux from a `linux` logical partition inside `super`, was built up over USB,
and was later copied block by block to `userdata`. The initramfs can still fall back to that
layout if `/etc/op9-linux.extents` exists. Building the filesystem on a PC, as in steps 5 and 6,
is simpler but hasn't been run on a real phone, so please report what happens.
