# Status

Tested on one T-Mobile LE2127 running kernel 7.3 with this patch series.

The modem features (mobile data, calls, SMS, MMS, GPS and call audio), the earpiece, the speaker
limiter and charging to full were tested on the current series. Wi-Fi, sleep, video decoding, Bluetooth,
vibration, the flashlight, NFC card detection, the buttons, the alert slider and wireless charging
were tested after memory scaling was added, before the modem was running. The other features were
tested on earlier builds of the series and haven't been rerun since.

## Numbers

| Measurement | Result |
|---|---|
| Memory copy, prime core | About 17 GB/s, 142 ns latency |
| 4K60 VP9 in Firefox, on battery | 5 runs of about 2 minutes: 0 firmware rejections |
| Dropped frames in those runs | 158–321 of about 7,000, mostly in the first 45 seconds |
| 4K60 VP9 on YouTube's mobile site | 32 dropped of 2,145 in the player's own stats |
| Firefox scrolling test, 30 s of a heavy page | 116 fps, up from 109–114; stalls over 100 ms 0–1, down from 1–6 |
| Sleep (s2idle), before the modem was running | About 40–44 mA, roughly 1.5 days of standby (estimate) |
| Overnight standby, modem running, no SIM | 41 mA average over 3 h 19 min unplugged, including 16 min awake before the idle suspend; then one uninterrupted 3 h sleep |
| GPS cold start, outdoors, no assistance data | 12 satellites in view after 45 s, fix after 65 s |
| USB 3 flash drive | 94.5 MB/s read |
| Swap | 5.4 GB of zram (zstd), compressed in RAM |
| Charging | 8.84 V at up to 2.77 A on a 25 W charger; 64% to 100% in 58 min, then stops, with the charger running the phone (+10 mA at the battery) |
| Wireless charging, from a Galaxy S26 Ultra's reverse charging | About 2.2 W in; up to +97 mA at the battery with the phone idle |

## Known problems

- **Sleep:** the chip doesn't reach its deepest sleep state, and "deep" suspend wakes by itself, so
  `s2idle` is used. The 40–44 mA figure was measured before the modem was running. With the modem
  running and no SIM, standby measured 41 mA, so the modem itself costs nothing measurable. Standby
  with a SIM registered for data and VoLTE hasn't been measured yet. `qcom_stats` in debugfs shows which
  subsystem keeps the chip out of its deepest state.
- **Wireless charging:** the temperature limits haven't been seen to trigger on wireless, since the
  phone stayed under 38 °C. They use the same code as USB. Using OTG while on a wireless charger isn't
  handled. Stock's faster wireless charging needs drivers for the P9415 and HL7227 chips.
- **Charging to full:** as stock, to 4.43 V per cell (8.86 V), stopping when the gauge reports full or
  under 125 mA, and recharging 100 mV per cell lower. From full until unplugged the battery shows
  "Full" at 100%, then falls to the gauge's value at 1% a minute, like Android's UI. Cell voltages
  aren't read, so the limits use half the pack voltage, where stock uses the higher cell. Phosh
  marks a full battery on its charger with Adwaita's "charged" icon, which also has a lightning
  bolt. fastfetch shows no status for a full battery and doesn't count a USB or wireless charger,
  so `rootfs/etc/xdg/fastfetch/config.jsonc` gets its battery line from `op9-battery-line`.
- **After charging:** the phone doesn't sleep while a charger is attached. If the idle timer already
  fired during that time, the phone stays awake with the screen off until it's next used, at about
  53 mA instead of about 40.
- **Charging notification:** Phosh lets any notification be swiped away, so it comes back silently
  within 5 seconds.
- **YouTube in Firefox:** the mobile page clips its left edge at the default zoom. Zooming that site
  to 80% fixes it. The cause wasn't measured. Wikipedia, BBC News and a test page fit correctly.
- **Waydroid:** Android can't use the cameras, phone calls or Bluetooth directly. Some apps refuse to
  run because Waydroid doesn't pass Google's device integrity checks. Waydroid also makes the camera
  and video decoder devices readable and writable by every user. Android 17 wasn't tried: no ARM64
  image exists, and others report its window never appears on Waydroid 1.6.3.
- **Docks:** two USB-C DisplayPort docks (one a Dell) gave working USB 3, Ethernet, keyboard and
  mouse, but no display. With `pmic_glink_altmode` logging on, the charger firmware reported only
  USB (SVID 0xff00) for the whole 18 minutes a dock was attached, never DisplayPort (0xff01). A
  USB-C to HDMI adapter goes from USB to DisplayPort with HPD within 1.5 s. The difference is the
  power role: the phone powers the adapter, so it is USB host and source from the start, while a
  dock charges the phone, so the phone has to swap to host first. The firmware doesn't seem to
  look for alternate modes after that swap. UCSI reports no alternate modes for either device, so
  Linux can't request one. Turn the logging on with `options pmic_glink_altmode dyndbg=+p` in
  `/etc/modprobe.d/`.
- **Steam:** x86 programs run through FEX without its library forwarding ("thunks", which need an
  x86 cross compiler to build), so games' graphics drivers run emulated too. Steam's first start,
  before the phone had swap, filled all 11 GB with its Chromium interface and froze the phone for
  minutes until the kernel killed a process. It hasn't been retried with zram yet. Half-Life 2's
  Linux version was played; no frame rates were measured.
- **GPU:** one GPU fault with recovery was seen in the compositor, before the memory scaling work.
  A second one on 2026-11-05 started when the GPU woke to dim the screen: the GMU stopped answering
  (`Timeout waiting for GMU OOB set GPU_SET`), the GPU faulted, and after the reset Phosh no longer
  answered the power key and the phone never suspended. It then drew about three times its awake idle
  current, screen off, for three hours until it was reset by hand. The cause isn't known. The
  kernel now reports hung tasks (3 minutes in D state), soft lockups and workqueue stalls, and
  `op9-lockup-watch` reboots after the report has reached the journal, with an immediate reset
  if shutdown hangs on the stuck task. Every reset on this phone clears RAM, so ramoops can't
  keep a panic log. Kernel panics (`rootfs/etc/sysctl.d/90-op9-lockup.conf`) are only the fallback
  for when userspace can't run. GPU crash dumps are saved to `/var/lib/op9-devcoredump`. Don't
  set `/sys/kernel/reboot/mode` to `warm`: that reboot hung twice in firmware.
- **Boot:** `systemd-backlight` sometimes fails at startup.
- **Driver unloading:** don't unload `qcom_iris` after a decoder hang, or `ath11k_pci` after a failed
  crash recovery. Either can hang shutdown, so reboot instead.
- **Clock:** the PMIC real-time clock is read-only, so `rtc-offset` stores an offset in a file.
- **GPS:** geoclue reports an accuracy of 0 m for modem fixes, because it doesn't compute accuracy
  from this modem's NMEA, so apps treat every fix as exact. Fixes measured outdoors stayed within
  about 11 m of their average. There's no assisted GPS yet, so a cold start takes about a minute;
  ModemManager can inject XTRA assistance data and set a SUPL server, which hasn't been tried.
- **Modem without a SIM:** the modem keeps running. Standby averaged 41 mA that way, the same as
  before the modem was brought up.
- **Modem storage is read-only:** `rmtfs` runs with `-r`, so the modem's writes to its storage
  stay in memory and the EFS partitions can never be changed from Linux. Anything the modem wants
  to remember across reboots is lost. This is deliberate; don't "fix" it.
- **SIM slots:** `op9-sim-provision` opens a session for the first USIM in slot 1 only. The second
  slot and eSIM haven't been tried. A SIM inserted while running needs the services restarted.
- **Call audio:** only the earpiece and speaker are routed during a call. A wired headset needs a
  voice route to the codec's RX path and a jack check in `op9-callaudio`. Bluetooth is harder: the
  call stays inside the audio DSP and never reaches PipeWire, so HFP can't carry it without a DSP
  Bluetooth port or a loopback through a host PCM. `op9-callaudio` also expects the phone's sound
  card at index 0, which a USB audio device or an HDMI adapter can shift.
- **Addresses:** `op9-addresses` reads the Wi-Fi and Bluetooth addresses from the modem at boot
  (`qmicli --dms-get-mac-address`, which answers about 6 s into boot once `rmtfs` has started the
  modem), so one kernel serves every phone. Without the modem services, Wi-Fi has the firmware's
  placeholder address and Bluetooth no adapter. The stock DTB blob the bootloader insists on is
  OnePlus's, so releases ship Image, modules and initramfs, and the boot image is packed on the
  user's PC from their own `vendor_boot.img`.
- **Headphone path:** opening the headphone output logs a SoundWire "port collision" on the RX
  bus. Speaker playback is unaffected. Not yet checked with an analog headset attached.

## Desktop mode

Plugging in a display (tested with a USB-C to HDMI adapter and a Samsung TV) opens a chooser on the
phone. Start desktop stops Phosh and its apps and runs a full GNOME session on the external
display, with the phone panel off and its touchscreen ignored. Unplugging, or no display showing
up within 20 s, brings Phosh back. The choice can be remembered.

- `op9-desktop` is both the chooser under Phosh and the helper under GNOME. It writes GNOME's
  `monitors.xml` from the display's EDID before starting, so GNOME never opens on the phone first.
- Between the two compositors, the kernel console redrew itself onto every screen and left planes
  that mutter can't move off it. `op9-kms-reset` unbinds the console and detaches leftover planes.
- Patch 0144 keeps DP off the mixers and CTL the panel needs. Without it, the panel came back on
  other blocks after desktop mode and never drew a frame until a reboot.
- GNOME scans out linear buffers: 10-bit UBWC-compressed ones showed black on DP.
- Not done yet: the phone screen as a keyboard and trackpad, docks (see Known problems) and MST.

## Fingerprint reader

The under-display Goodix sensor doesn't appear to be something Linux can drive directly. On stock,
a 13 MB Qualcomm secure app (`goodixfp`) runs in the Trusted Execution Environment, and Android's
closed libraries reach it through `QSEECom_start_app`, then sync the display's bright flash with
each scan. Supporting it would need kernel support for loading and serving secure apps, a way to
run the Android libraries, display sync and a bridge to `fprintd`. The secure app does run with an
unlocked bootloader, since fingerprint unlock worked on this phone under LineageOS. Support hasn't
been attempted on Linux.

## Next

1. Measure standby with a SIM registered for data and VoLTE, then reach deeper sleep, including
   cutting touch power
2. Call audio through wired headsets, then Bluetooth
3. Assisted GPS for faster fixes, and real accuracy figures through geoclue
4. A prebuilt root filesystem tarball in the releases, now that one kernel serves every phone,
   and the install replayed from scratch with it
5. Get a display from USB-C docks
6. Test an analog USB-C headset and a Bluetooth headset's microphone
7. Improve camera quality and check video recording
8. Add a user-facing NFC app (`neard` isn't in the Arch Linux ARM repos)
9. Build the `userspace/` packages in CI on an ARM runner, so the install guide's package steps are
   checked on every change
