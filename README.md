# Arch Linux ARM on the OnePlus 9 Pro

Native Arch Linux ARM on the OnePlus 9 Pro (`lemonadep`, Snapdragon 888 / SM8350) with a
mainline Linux 7.3 kernel, the Phosh phone interface and no Android underneath.

This runs on one real phone. It isn't a polished distribution yet: there's no
installer or prebuilt image, and installing it means building a kernel and wiping the phone.

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
| Display | ✅ | 1440x3216 and 1080x2412 at up to 120 Hz |
| Touch | ✅ | 10 points |
| GPU | ✅ | OpenGL 4.6, ES 3.2, Vulkan 1.3 |
| Wi-Fi | ✅ | Up to 6 GHz, 160 MHz |
| USB-C | ✅ | Device and host modes, 5 Gbps USB 3 |
| HDMI over USB-C | ✅ | With a USB-C to HDMI adapter. Two USB-C docks gave USB 3 but no display |
| Charging | ✅ | USB-C PD, about 25 W; stops near 95% by choice. Plugging in wakes the phone, and it stays awake while charging so the temperature limits keep running |
| Wireless charging | 🟡 | Slow: about 2 W, tested only on a phone's reverse charging. Works the same as USB otherwise; USB takes over if both are attached |
| Charging status | ✅ | The screen lights up with a buzz when charging starts, and the lock screen shows the type and speed |
| Speakers and mics | ✅ | A limiter and high-pass filter guard the speakers; it's not stock's full speaker protection |
| Earpiece | ✅ | The top speaker in stock's receiver mode, chosen as a separate audio profile; loudness not yet checked by ear |
| Sensors | ✅ | Accelerometer, light, proximity, compass, auto-rotate |
| Video decoding | ✅ | H.264 and VP9 in hardware up to 4K60, including Firefox (no AV1 hardware on this chip) |
| Memory and CPU scaling | ✅ | DDR, LLCC and L3 scale with load; the scheduler knows which cores are fast |
| Bluetooth | ✅ | Pairing and music tested with a headset, headset mic untested; Bluetooth LE scanning works; each phone must set its own address |
| Buttons | ✅ | Power, volume up and volume down |
| Alert slider | ✅ | Top silent, middle vibrate, bottom ring |
| Vibration | ✅ | Works with Phosh feedback, including keyboard buzz |
| Flashlight | ✅ | Quick settings toggle with adjustable brightness |
| NFC | 🟡 | Detects cards; no app included, and tap-to-pay isn't possible |
| Cameras | 🟡 | Main, ultrawide, telephoto and front work with autofocus, using software processing and basic tuning. Mono is raw only. |
| Firefox | ✅ | Phone layout with mobile sites, using `mobile-config-firefox`; smooth after the scheduler fix. YouTube's mobile page clips its left edge unless zoomed to 80% |
| Android apps | ✅ | Waydroid with Android 13 (LineageOS 20): GPU acceleration, internet, sound and Google Play. A **Stop Android** launcher shuts it down |
| App launching | ✅ | No loading splash, since apps here usually open before it would go away |
| Sleep | 🟡 | Sleeps and wakes at about 40–44 mA, but the chip doesn't reach its deepest sleep state |
| USB-C analog headset | 🟡 | Routing is set up; untested without an analog adapter |
| Modem, calls, SMS, mobile data | ❌ | Modem processor is offline |
| GPS | ❌ | Not working; probably depends on the modem |
| Fingerprint | ❌ | Runs through a locked secure app; see [STATUS](docs/STATUS.md) |

More detail is in [docs/STATUS.md](docs/STATUS.md).

## What's here

| Folder | Contents |
|---|---|
| [`kernel/`](kernel) | 114 patches on Linux v7.3, the kernel config and a build script |
| [`initramfs/`](initramfs) | The early boot environment, which provides a USB rescue shell |
| [`rootfs/`](rootfs) | Config and services to copy onto Arch Linux ARM |
| [`userspace/`](userspace) | PKGBUILDs and patches for libcamera, hexagonrpc, iio-sensor-proxy, phoc and wlroots |
| [`firefox/`](firefox) | The FFmpeg patch and shim that give Firefox hardware video decoding, and its settings |
| [`tools/`](tools) | Source for helper tools |
| [`docs/`](docs) | [Install guide](docs/INSTALL.md), [status](docs/STATUS.md) and [hardware notes](docs/HARDWARE.md) |

**Firmware isn't included.** It's proprietary, so you copy it from your own phone. Sensor
calibration and the Wi-Fi and Bluetooth addresses are specific to each phone, so those come from your phone too.

## Getting started

Follow [docs/INSTALL.md](docs/INSTALL.md). You'll need a Linux PC, an unlocked bootloader, an
`aarch64-linux-gnu` cross compiler and experience recovering a phone from fastboot.

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
- The Linux, Arch Linux ARM, postmarketOS, Phosh, libcamera and linux-msm communities

## License

GPL-2.0-only, to match the kernel (see [LICENSE](LICENSE)). Patches for other projects are
under those projects' licenses.
