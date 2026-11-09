# Arch Linux ARM on the OnePlus 9 Pro

[![Kernel build](https://github.com/LuCiLuna/oneplus9pro-arch/actions/workflows/kernel.yml/badge.svg)](https://github.com/LuCiLuna/oneplus9pro-arch/actions/workflows/kernel.yml)

Native Arch Linux ARM on the OnePlus 9 Pro (`lemonadep`, Snapdragon 888 / SM8350) with a
mainline Linux 7.3 kernel, the Phosh phone interface and no Android underneath.

This runs on one real phone. It isn't a polished distribution yet: releases carry a kernel that
runs on every phone, but there's no installer or prebuilt root filesystem, and installing means
wiping the phone.

## ⚠️ Read this first

- **Installing erases Android and everything on the phone.** Back up your partitions first,
  especially `persist`, `modem` and the EFS partitions. They hold data unique to your phone.
- **You can brick your phone,** and no one here can repair it.
- **Only one phone has been tested:** a T-Mobile **LE2127** (project `2085A`). Other models may
  need different device tree IDs or firmware.
- **The install steps have never been replayed from scratch.** They're written from how this
  phone got here, so expect to troubleshoot.
- **Never write to** `xbl`, `persist`, `modem`, the EFS partitions or `param`.

## Status

| Feature | Status | Notes |
|---|---|---|
| Display | ✅ | 1440x3216 and 1080x2412 at up to 120 Hz. Stock's sunlight boost (600-800 nits) switches on in bright light. A quick setting picks Natural (sRGB) or Vivid colour |
| Touch | ✅ | 10 points. A quick setting raises the report rate from 120 Hz to stock's 360 Hz |
| GPU | ✅ | OpenGL 4.6, ES 3.2, Vulkan 1.3 |
| Wi-Fi | ✅ | Up to 6 GHz, 160 MHz; the phone's own address is read from the modem at boot |
| USB-C | ✅ | Device and host modes, 5 Gbps USB 3 |
| HDMI over USB-C | ✅ | With a USB-C to HDMI adapter. Two USB-C docks gave USB 3 but no display |
| Desktop mode | 🟡 | Plugging in a display offers to open full GNOME on it, with the phone screen as a keyboard and trackpad that vibrate under your fingers. Docks and MST hubs don't work yet |
| Charging | ✅ | USB-C PD, about 25 W, to stock's limits: stops at 100% and reports the battery full until unplugged. Plugging in wakes the phone, and it stays awake while charging so the temperature limits keep running |
| Wireless charging | 🟡 | Slow: about 2 W, tested only on a phone's reverse charging. Works the same as USB otherwise; USB takes over if both are attached |
| Charging status | ✅ | The screen lights up with a buzz when charging starts, and the lock screen shows the type and speed. fastfetch shows USB or Wireless Connected |
| Speakers and mics | ✅ | A limiter and high-pass filter guard the speakers; it's not stock's full speaker protection |
| Earpiece | ✅ | The top speaker in stock's receiver mode, chosen as a separate audio profile; loudness not yet checked by ear |
| Sensors | ✅ | Accelerometer, light, proximity, compass, auto-rotate |
| Video decoding | ✅ | H.264 and VP9 in hardware up to 4K60, including Firefox (no AV1 hardware on this chip) |
| Memory and CPU scaling | ✅ | DDR, LLCC and L3 scale with load; the scheduler knows which cores are fast |
| Bluetooth | ✅ | Pairing and music tested with a headset, headset mic untested; Bluetooth LE scanning works; the phone's own address is read from the modem at boot |
| Buttons | ✅ | Power, volume up and volume down |
| Alert slider | ✅ | Top silent, middle vibrate, bottom ring |
| Vibration | ✅ | Works with Phosh feedback, including keyboard buzz |
| Flashlight | ✅ | Quick settings toggle with adjustable brightness |
| NFC | 🟡 | Detects cards; no app included, and tap-to-pay isn't possible |
| Cameras | 🟡 | Main, ultrawide, telephoto and front work with autofocus, using software processing with stock's colour matrices. Mono is raw only. |
| Firefox | ✅ | Phone layout with mobile sites, using `mobile-config-firefox`; smooth after the scheduler fix. YouTube's mobile page clips its left edge unless zoomed to 80% |
| Android apps | ✅ | Waydroid with Android 13 (LineageOS 20): GPU acceleration, internet, sound and Google Play. A **Stop Android** launcher shuts it down |
| App launching | ✅ | No loading splash, since apps here usually open before it would go away |
| Sleep | 🟡 | Sleeps and wakes at about 40–44 mA, measured before the modem was running; the chip doesn't reach its deepest sleep state |
| USB-C analog headset | 🟡 | Routing is set up; untested without an analog adapter |
| Mobile data | ✅ | LTE, IPv4 and IPv6, tested on AT&T |
| Calls | ✅ | VoLTE on AT&T, both directions, with audio through the earpiece; speaker and mute from the call screen. Headsets and Bluetooth aren't routed during calls yet |
| SMS | ✅ | Sending and receiving, over IMS on AT&T |
| MMS | ✅ | Sending and receiving pictures with mmsd-tng and Chatty, on AT&T |
| GPS | ✅ | Apps get it through geoclue; first fix in about a minute outdoors without assistance data, fixes within about 11 m of each other. Turn on Location Services in Settings |
| x86 games | 🟡 | Steam and Half-Life 2's Linux version run through the FEX emulator; Steam's interface is slow to start. Optional, see the [install guide](docs/INSTALL.md) |
| Fingerprint | ❌ | Driven by TrustZone secure apps; the apps load on mainline but enroll hit a boot-time secure-storage wall, see [FINGERPRINT](docs/FINGERPRINT.md) |

More detail is in [docs/STATUS.md](docs/STATUS.md).

## What's here

| Folder | Contents |
|---|---|
| [`kernel/`](kernel) | 148 patches on Linux v7.3, the kernel config and a build script |
| [`initramfs/`](initramfs) | The early boot environment, which provides a USB rescue shell |
| [`rootfs/`](rootfs) | Config and services to copy onto Arch Linux ARM |
| [`userspace/`](userspace) | PKGBUILDs and patches for libcamera, hexagonrpc, iio-sensor-proxy, phoc and wlroots, the modem services and FEX |
| [`firefox/`](firefox) | The FFmpeg patch and shim that give Firefox hardware video decoding, and its settings |
| [`scripts/`](scripts) | Packing a boot image from a release and your phone's `vendor_boot.img`, and USB internet sharing |
| [`tools/`](tools) | Source for helper tools |
| [`docs/`](docs) | [Install guide](docs/INSTALL.md), [status](docs/STATUS.md) and [hardware notes](docs/HARDWARE.md) |

**Firmware isn't included.** It's proprietary, so you copy it from your own phone, and so is the
stock device tree blob the bootloader insists on, which the boot image script takes from your
`vendor_boot.img`. Sensor calibration is specific to each phone, so it comes from your phone too.
The Wi-Fi and Bluetooth addresses are read from the modem at boot.

## Getting started

Follow [docs/INSTALL.md](docs/INSTALL.md). You'll need a Linux PC, an unlocked bootloader and
experience recovering a phone from fastboot, plus an `aarch64-linux-gnu` cross compiler if you
build the kernel yourself rather than taking it from a [release](https://github.com/LuCiLuna/oneplus9pro-arch/releases).

## Contributing

Reports from other models are especially useful. If you try this, please open an issue with your
model number, your firmware version and what happened, working or not.

## Patch quality

Many patches are marked `WIP` or `HACK` because they get this phone working but aren't ready for
upstream. Please review them before relying on them or submitting them elsewhere.

Much of this port was developed with Claude Code (Anthropic), and those commits say so in their
`Co-Authored-By` trailers.

## Credits

This builds on other people's work, and their authorship is kept in the patches:

- **Nia Espera** and **Caleb Connolly**, and the
  [sm8350-mainline](https://gitlab.com/sm8350-mainline) project, for the original port
- **David Heidelberg** for the CAMSS C-PHY series
- **Vsevolod Nevorotov** for the Synaptics TouchComm driver
- **Stephan Gerhold**, **Richard Acayan**, **Vincent Knecht**, **Otto Pflüger**, **Bhushan Shah** and
  **Val Packett** for the q6voice call audio series, as carried by
  [sc7280-mainline](https://gitlab.com/sc7280-mainline) for the Fairphone 5
- **postmarketOS** for `81voltd` and `mobile-config-firefox`, and **linux-msm** for `rmtfs`,
  `tqftpserv`, `qrtr` and `hexagonrpc`
- The Linux, Arch Linux ARM, postmarketOS, Phosh, libcamera and linux-msm communities

## License

GPL-2.0-only, to match the kernel (see [LICENSE](LICENSE)). Patches for other projects are
under those projects' licenses.
