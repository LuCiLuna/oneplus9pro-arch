# Status

Tested on one T-Mobile LE2127 running kernel 7.3 with this patch series.

Wi-Fi, sleep, video decoding, Bluetooth, vibration, the flashlight, NFC card detection, the buttons,
the alert slider and wireless charging were tested after memory scaling was added. The other features were tested on earlier builds of the
series and haven't been rerun since.

## Numbers

| Measurement | Result |
|---|---|
| Memory copy, prime core | About 17 GB/s, 142 ns latency |
| 4K60 VP9 in Firefox, on battery | 5 runs of about 2 minutes: 0 firmware rejections |
| Dropped frames in those runs | 158–321 of about 7,000, mostly in the first 45 seconds |
| Firefox scrolling test, 30 s of a heavy page | 116 fps, up from 109–114; stalls over 100 ms 0–1, down from 1–6 |
| Sleep (s2idle) | About 40–44 mA, roughly 1.5 days of standby (estimate) |
| USB 3 flash drive | 94.5 MB/s read |
| Charging | 8.84 V at up to 2.77 A on a 25 W charger |
| Wireless charging, from a Galaxy S26 Ultra's reverse charging | About 2.2 W in; up to +97 mA at the battery with the phone idle |

## Known problems

- **Sleep:** the chip doesn't reach its deepest sleep state, and "deep" suspend wakes by itself, so
  `s2idle` is used.
- **Wireless charging:** the temperature limits haven't been seen to trigger on wireless, since the
  phone stayed under 38 °C. They use the same code as USB. Using OTG while on a wireless charger isn't
  handled. Stock's faster wireless charging needs drivers for the P9415 and HL7227 chips.
- **After charging:** the phone doesn't sleep while a charger is attached. If the idle timer already
  fired during that time, the phone stays awake with the screen off until it's next used, at about
  53 mA instead of about 40.
- **Charging notification:** Phosh lets any notification be swiped away, so it comes back silently
  within 5 seconds.
- **Docks:** one USB-C dock tested failed both DisplayPort and USB 3. A plain USB-C to HDMI adapter
  and a USB 3 OTG adapter both work.
- **GPU:** one GPU fault with recovery was seen in the compositor, before the memory scaling work.
- **Boot:** `systemd-backlight` sometimes fails at startup.
- **Driver unloading:** don't unload `qcom_iris` after a decoder hang, or `ath11k_pci` after a failed
  crash recovery. Either can hang shutdown, so reboot instead.
- **Clock:** the PMIC real-time clock is read-only, so `rtc-offset` stores an offset in a file.

## Fingerprint reader

The under-display Goodix sensor doesn't appear to be something Linux can drive directly. On stock,
a 13 MB Qualcomm secure app (`goodixfp`) runs in the Trusted Execution Environment, and Android's
closed libraries reach it through `QSEECom_start_app`, then sync the display's bright flash with
each scan. Supporting it would need kernel support for loading and serving secure apps, a way to
run the Android libraries, display sync and a bridge to `fprintd`. The secure app does run with an
unlocked bootloader, since fingerprint unlock worked on this phone under LineageOS. Support hasn't
been attempted on Linux.

## Next

1. Improve camera quality and check video recording
2. Reach deeper sleep, including cutting touch power
3. Add the earpiece, USB-C headset and speaker protection
4. Use a phone-friendly Firefox interface
5. Add a user-facing NFC app (`neard` isn't in the Arch Linux ARM repos)
6. Bring up the modem
