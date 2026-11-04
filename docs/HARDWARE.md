# Hardware notes

What was found while bringing up this phone, for anyone working on SM8350 or similar devices.

## Boot

- The bootloader (ABL) rejects mainline-style device trees. It has to apply the stock DTBO to real
  downstream nodes. So the image carries the stock DTBs, and `CONFIG_ARM64_OP9_BUILTIN_DTB` swaps in
  the mainline one at boot.
- Images must be boot header v2 with an uncompressed `Image`.
- The firmware runs the Gunyah hypervisor. Its watchdog resets the phone after about 10 seconds
  unless it's handled early in boot.
- The boot GPT is on a UFS LUN with 4096-byte sectors and only 96 entries. Don't let `sgdisk` rewrite
  it. `tools/op9-slot.c` edits just the slot attribute bits.

## Components

| Part | Details |
|---|---|
| Panel | Samsung AMB670YF01, DSC command mode |
| Touch | Synaptics S3908, I2C4 at `0x4b`; GPIO 197 is an eGPIO |
| GPU | Adreno 660 |
| Wi-Fi and Bluetooth | QCA6490 v1, driven as a WCN6855 in `ath11k` with trimmed firmware memory |
| Speakers | 2x NXP TFA9874, I2C19 at `0x34` (top) and `0x35` (bottom) |
| Codec | WCD9380 over SoundWire; phone mics are DMIC1 and DMIC3 |
| Alert slider | 2x IST8801 hall sensors, I2C15 at `0x19` (up) and `0x18` (down), IRQ GPIOs 50 and 51 |
| Vibration | Awinic AW8697, I2C17 at `0x5a`, reset GPIO 75, 170 Hz motor |
| Flash | PM8350C flash controller, channels 1 and 2 |
| NFC | NXP SN100T, I2C15 at `0x28`, NCI 2.0; IRQ GPIO 87, enable GPIO 62, firmware GPIO 86 |
| Fingerprint | Goodix optical G3S, behind a secure app; not supported |
| Battery gauge | BQ28Z610, 2 cells in series |
| Wireless charging | IDT P9415 receiver at `0x3b` and HL7227 charge pump at `0x51`, on the I2C at `0x980000`; no drivers, the charger firmware handles basic power. PM8350C GPIO 2 turns the receiver off when high: the bootloader sets it if USB is attached at boot, and the battery driver keeps it off while USB is attached |
| Main camera | Sony IMX789, 3-trio C-PHY, AK7377 focus at `0x0c` |
| Ultrawide | Sony IMX766, C-PHY, LC898229 focus at `0x72` |
| Telephoto | OmniVision OV08A10, 2-lane D-PHY, AK7377 focus at `0x0f` |
| Mono | GalaxyCore GC02M1B, 1-lane |
| Front camera | Sony IMX471 |
| Sensors | On the SLPI, reached with hexagonrpcd and libssc |
| Modem | Integrated in the SM8350, runs `modem.mbn` with the carrier configs from `modem_pr`; storage from `modemst1`, `modemst2`, `fsg` and `fsc` through `rmtfs`. Voice, SMS, data, SIM and location services over QRTR |
| USB-C audio switch | FSA4480 on the SBU lines, for analog headset adapters |

## Things that cost a lot of time

- **Memory scaling:** mainline `sm8350.dtsi` had no bandwidth monitors, no CPU or L3 votes, and no GPU
  memory path, so DDR stayed slow under load (3–4 GB/s memcpy). 4K60 video then made the video
  firmware time out while parsing VP9 headers.
- **CPU capacity:** `sm8350.dtsi` had no `capacity-dmips-mhz`, so every core reported the same capacity
  and browser work landed on A55s by chance. Energy-aware scheduling then made scrolling worse, so
  `rootfs/etc/sysctl.d/90-op9-sched.conf` turns it off.
- **Firmware log:** gen1 Iris firmware reports useful errors if you send `HFI_PROPERTY_SYS_DEBUG_CONFIG`.
  That's what exposed the VP9 timeout.
- **Display flashes:** in command mode with autorefresh, `CTL_START` never fires, so flip events
  went out before the flush latched and the compositor painted over the buffer being scanned out.
- **Wi-Fi firmware crashes:** PCIe had no interconnect votes, so the link starved once boot votes
  were dropped.
- **Sleep drain:** the MI2S bit clock was never stopped in the DSP, so the ADSP couldn't sleep.
- **USB replug:** charger detection takes over DP/DM and leaves the PHY in a state that stops
  re-enumeration, so the PHY datapath has to be reclaimed.
- **OTG:** the charger firmware keeps Type-C sink-only until the AP sets the OnePlus-specific properties.
- **Bluetooth address:** the chip has none, so the kernel leaves the controller unconfigured and BlueZ
  sees nothing. `btmgmt --index 0 public-addr` configures it at runtime. Stock reads it from the
  modem over QMI, and so does `op9-addresses`: `qmicli -p -d qrtr://0 --dms-get-mac-address=bt`
  (and `=wlan` for Wi-Fi) returns the addresses byte-reversed, about 6 s into boot after `rmtfs`,
  without ModemManager.
- **Modem bring-up:** the kernel side was complete; the modem only needed `rmtfs`, `tqftpserv` and
  ModemManager in userspace. Three things stock's radio layer does that nothing on Linux did:
  open the SIM's provisioning session (without it ModemManager reports "GW primary session index
  unknown"), answer the modem's request on QMI service 770 to bring up the `ims` data connection
  before IMS registers (`81voltd`), and register as the location service's "master client" with a
  fixed key before starting GPS (otherwise every start request fails with GeneralError).
- **Call audio:** the voice stream never reaches the AP. It's routed inside the audio DSP, like stock's
  `voicemmode1-call`: from the modem to TERT_MI2S_RX (the speaker amplifiers) and from
  TX_CODEC_DMA_TX_3 (the microphones). Opening the hostless VoiceMMode1 PCM starts the session.
- **Earpiece:** it's the top TFA9874 in stock's "receiver" profile: boost converter off, input 6 dB
  down, speaker gain code 13, low-noise mode, bottom amplifier off.
- **Unsafe register access:** reading unclocked display or LPASS registers from Linux, for example
  through `/dev/mem`, hangs the phone. Only use `tools/regdump.c` if you know the clocks are on.
