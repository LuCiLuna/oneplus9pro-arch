# Status

Tested on one T-Mobile LE2127 running kernel 7.3 with this patch series.

Wi-Fi, sleep and video decoding were tested on the current kernel. The other features were
tested on earlier builds of the series and haven't been rerun since memory scaling was added.

## Numbers

| Measurement | Result |
|---|---|
| Memory copy, prime core | About 17 GB/s, 142 ns latency |
| 4K60 VP9 in Firefox, on battery | 5 runs of about 2 minutes: 0 firmware rejections |
| Dropped frames in those runs | 158–321 of about 7,000, mostly in the first 45 seconds |
| Sleep (s2idle) | About 40–44 mA, roughly 1.5 days of standby (estimate) |
| USB 3 flash drive | 94.5 MB/s read |
| Charging | 8.84 V at up to 2.77 A on a 25 W charger |

## Known problems

- **Firefox:** scrolling and interaction stutter. Cause not yet found.
- **Sleep:** the chip doesn't reach its deepest sleep state, and "deep" suspend wakes by itself, so
  `s2idle` is used.
- **Docks:** one USB-C dock tested failed both DisplayPort and USB 3. A plain USB-C to HDMI adapter
  and a USB 3 OTG adapter both work.
- **GPU:** one GPU fault with recovery was seen in the compositor, before the memory scaling work.
- **Boot:** `systemd-backlight` sometimes fails at startup.
- **Driver unloading:** don't unload `qcom_iris` after a decoder hang, or `ath11k_pci` after a failed
  crash recovery. Either can hang shutdown, so reboot instead.
- **Clock:** the PMIC real-time clock is read-only, so `rtc-offset` stores an offset in a file.

## Next

1. Finish Bluetooth
2. Fix Firefox stutter
3. Improve camera quality and check video recording
4. Add vibration and the flashlight
5. Reach deeper sleep, including cutting touch power
6. Add the fingerprint reader, earpiece, USB-C headset and speaker protection
7. Bring up the modem
