# shimmer3-firmware

Firmware for the **Shimmer3** platform, targeting the **MSP430F5437A**. This is the public repo.

`README.md` and `LogAndStream_Shimmer3/README.md` are the reference; this file covers what they don't.

## Projects
- **`LogAndStream_Shimmer3/`** — the actively maintained firmware. Simultaneous SD logging and
  Bluetooth streaming (classic, and BLE via RN4678).
- **`S3_Sleep/`** — minimal lowest-power firmware; a reasonable starting point for custom work.

**BtStream and SDLog are deprecated** as standalone firmwares. SDLog still compiles from this tree,
but everything it does is in LogAndStream and configurable there. Don't start new work on either.

## The shared submodule ships to two MCUs
`LogAndStream_Shimmer3/log-and-stream-common` is its own repo
(`ShimmerResearch/log-and-stream-common`) and is **also a submodule of `shimmer3r-firmware`**, the
STM32U5 Shimmer3R firmware. Anything changed there lands on both platforms.

That matters most in this direction: this is the **MSP430** side. Code written against STM32 word
sizes, timer behaviour or toolchain builtins will break the build here, and nothing in the common
repo warns you at edit time. Platform-specific behaviour belongs behind the abstraction in
`log_and_stream_externs.h`.

## Build
| | |
|---|---|
| IDE | Code Composer Studio **v12.8.1.00005** |
| Compiler | TI MSP430 **v21.6.1.LTS** |

Version pinning is deliberate — check the README table before upgrading either.

## Release
CI only: `build-release-firmware.yml` via **workflow_dispatch** (major/minor/patch, Release/Debug).
The push trigger is commented out on purpose, so releases are never accidental. `FirmwareIdentifierList.txt`
is the firmware identifier registry — keep it in step when adding a build.

`clang-format-check.yml` gates pushes; it checks rather than reformatting, so bad formatting fails CI
instead of being silently fixed.
