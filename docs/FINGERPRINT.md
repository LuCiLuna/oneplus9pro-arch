# Fingerprint reader (Goodix G3S)

The under-display sensor is a Goodix G3S optical reader. There is no Linux driver that talks to it
directly: on stock Android the whole thing is driven by Qualcomm TrustZone *trusted apps* (TAs)
running in the secure world, and the Linux side only loads them, serves their callbacks, and syncs
the screen's bright flash with each scan.

This page records how far a native-Linux attempt got and the exact wall it hit, so the next person
doesn't re-tread it. **Short version:** the secure apps load and run on mainline, the secure-storage
relays they need work, and the enrollment failure was traced to a hard TrustZone anti-rollback wall
bound to the phone's stock boot-time secure state — not to the sensor, and not to the relay.

## How the stock stack is put together

Two secure apps matter:

- **`goodixfp`** — talks to the sensor, does the actual image capture and matching.
- **`keymaster64`** — hosts Qualcomm's *gatekeeper*, which mints the **hardware auth token (HAT)**
  that authorizes an enroll or a verify. Without a valid HAT the fingerprint flow can't enroll.

The gatekeeper keeps its state (password/credential handles, anti-rollback version) in **secure
storage**: a small secure file system backed by the UFS **RPMB** (Replay-Protected Memory Block)
partition, which only authenticated, monotonically-counted writes can touch. Android serves all of
this through its `qseecom` driver plus userspace listeners that the TAs call back into (one for RPMB,
one for the file system). A separate piece syncs the display's flash with each capture.

## What was built on mainline

- A **`qcom_qseecom_mobile` kernel driver** exposing `/dev/qseecom_mobile`, with ioctls to load a TA,
  send it a command, and register/receive/send the listener callbacks the TA makes back to userspace.
- **Userspace listeners** the TAs call into: an **RPMB listener** that relays the TA's authenticated
  RPMB frames to the UFS RPMB well-known LUN, and a **file-system listener**.
- Small tools to drive the gatekeeper/keymaster commands directly for bring-up.

With these, the stock TAs **load and run on mainline** (`keymaster64` loads as app id 1 and answers
commands), and the RPMB listener reads and writes the phone's real secure storage.

## The RPMB relay — and a fix worth reusing

The TAs' secure storage lives in the UFS RPMB WLUN. The relay speaks SCSI `SECURITY PROTOCOL
IN/OUT` (`0xA2`/`0xB5`), security protocol `0xEC`, with the length expressed in bytes (the `INC_512`
bit **off** — setting it returns *invalid field in CDB* on this UFS part), issued as `sg_io_v4`
against `/dev/bsg`. Reads come back as correct, MAC-carrying JEDEC RPMB frames.

Writes were at first intermittent: the first write after an idle gap would fail with a SCSI **CHECK
CONDITION**. Capturing the sense data identified it as **UNIT ATTENTION, ASC `0x29/00` (power-on /
reset)** — the RPMB LUN reporting that it had reset, which the SCSI spec clears on the command that
reports it. **Retrying the write once clears it**, after which writes land reliably: the RPMB write
counter advances and the device returns result `0x0000`. Anyone relaying RPMB from userspace on this
platform should retry on that UNIT ATTENTION; it is otherwise a confusing source of flakiness.

## Where it's blocked: gatekeeper enroll

Enrolling fails inside the gatekeeper at its storage init (`gk_storage_platform_init`), which
propagates up to a credential-encryption failure and a `-30` back to the HAL.

The cause was traced into the keymaster TA itself, which ships as an **unencrypted aarch64 ELF**. The
failing step is the return value of **`qsee_stor_read_sectors` / `qsee_stor_write_sectors`** —
Qualcomm's TEE storage API, which lives in `cmnlib64` (bundled inside the `tz` image). These return
an error **even when the underlying RPMB operation itself succeeds** (`result=0x0000`, and for writes
the counter advances). In other words, the RPMB relay does its job; the TEE storage layer on top of
it rejects the result.

The reason is **anti-rollback**. The secure file system versions its storage and binds that version
to monotonic RPMB state (strings in the `tz` image: `version_Read_And_Verify_Rpmb_File`,
`version_Init_Rpmb_Partition`, a "rpmb version rollback label", `antiRollback.protectionLevel`). This
phone's storage was last written by **stock OxygenOS**. On mainline — unlocked, with a different
secure-boot state — the TEE reads a version baseline it establishes at **TrustZone boot** and caches,
finds it inconsistent, and refuses the storage.

## Why it can't be worked around from Linux

The TA *does* contain a recovery path: if opening its storage partition (identified by a `"22KG"`
magic) fails, it calls `qsee_stor_add_partition` to **create a fresh one**, which would re-baseline
the anti-rollback at the current counter and fix everything. The catch:

- The partition open and the version baseline are read **at TrustZone boot, before Linux exists**,
  and cached in secure memory. They never pass through the runtime RPMB listener — the first listener
  call a gatekeeper operation makes is already a data read/write, never the partition open. So Linux
  can neither make the open fail nor reset the cached version.
- It is **not a key wall**: reads verify and writes land, so the RPMB key is valid. It's a boot-time
  secure-state wall.
- The TA and the `tz` image are **signed**, so patching them out isn't an option.

## The open direction

The realistic path is a **userspace gatekeeper**: synthesize the hardware auth token the `goodixfp`
HAL expects without depending on the QSEE gatekeeper's RPMB-backed storage, and drive the `goodixfp`
TA directly. The sensor and its secure app still do the real capture and matching; only the
storage-bound gatekeeper — the one piece fused to stock's boot-time secure state — is sidestepped.
The display-flash sync and an `fprintd` bridge would still be needed on top.

## Status

| Piece | State |
| --- | --- |
| Loading and running the stock TAs on mainline | Works (custom `qcom_qseecom_mobile` + listeners) |
| RPMB secure-storage relay | Works, and robust (UNIT ATTENTION retry) |
| Gatekeeper enroll | Blocked by TrustZone anti-rollback bound to stock's boot-time secure state |
| Userspace-HAT path | Not started — the open direction |
| `fprintd` bridge | Not started |

The driver and listeners used for this are experimental bring-up tools and aren't part of the
published kernel patches yet.
