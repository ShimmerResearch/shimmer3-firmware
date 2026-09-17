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

## Testing
The shared submodule carries a host test suite that needs no MSP430 toolchain and no device:

```
make -C LogAndStream_Shimmer3/log-and-stream-common/Test/host platform-check   # compile for BOTH MCUs
make -C LogAndStream_Shimmer3/log-and-stream-common/Test/host                  # build and run, ~10 s
```

`platform-check` is the one that matters most from here: it compiles the shared modules for
`-DSHIMMER3` *and* `-DSHIMMER3R`, which is the cheapest guard against a submodule change that builds
for one platform and not the other.

**It cannot stand in for a CCS build.** The host compiler's `int` is 32 bits and the MSP430's is 16,
so an expression like `uint8_var * 3600` overflows here and passes there. That class of fault is
caught only by the real build — see `log-and-stream-common/docs/SHIMMER3_TEST_PROCEDURE.md` §3.2.

The full release procedure — gates, the radio bring-up matrix, per-model functional
tests, sign-off list — is
`log-and-stream-common/docs/SHIMMER3_TEST_PROCEDURE.md`.

## Release
CI only: `build-release-firmware.yml` via **workflow_dispatch** (major/minor/patch, Release/Debug).
The push trigger is commented out on purpose, so releases are never accidental. `FirmwareIdentifierList.txt`
is the firmware identifier registry — keep it in step when adding a build.

**Releases are built from the `Debug` configuration.** `Release` does not build; known and parked —
the release workflow's `build_mode` input defaults to `Debug` for that reason. See
`log-and-stream-common/docs/SHIMMER3_BUILD_AND_PROGRAMMING.md` §4.2.

`clang-format-check.yml` runs on every push with `inplace: True` and commits the reformatted result
back, so a badly formatted push is fixed on your branch rather than rejected. **Pull before your next
push, and fetch before tagging a release**, or the tag misses the formatting commit. That commit also
gets no CI run of its own — GitHub does not trigger workflows for `GITHUB_TOKEN` pushes.

**Run `.githooks\install.bat` (or `.githooks/install.sh`) once per clone and the bot commit never
appears.** The `pre-commit` hook clang-formats the `.c`/`.h` files staged for the commit and re-stages
them. Nothing needs installing: Git for Windows supplies the shell, and
`Extras/clang-format-all-win64/clang-format.exe` is already in the clone. It never blocks a commit,
and `git commit --no-verify` bypasses it — see `.githooks/README.md`.

The installer also configures the `log-and-stream-common` submodule, because commits made inside it
are its commits and need their own hook configuration.

`Extras/clang-format-all-win64/LogAndStream-Shimmer3.bat` still formats the whole project in one go.
Its exclusion (`Shimmer_Driver/FatFs`) matches both the workflow's and the hook's, so all three format
the same set of files — **if you change one, change the other two in the same commit.**

CI pins clang-format **17**, the bundled `clang-format.exe` is **18.1.8**, and the two currently agree
on this codebase — the difference is not a live problem, but keep it in mind before blaming churn on
it. `.clang-format` lives in `LogAndStream_Shimmer3/`, not at the repo root.

## Keep the docs in step with the code
This repo has no `docs/` of its own — the reference documentation lives in the
`log-and-stream-common` submodule under `docs/`, and covers both platforms.

The rule there applies to changes made from here: **change behaviour in a subsystem, update its doc
in the same PR.** The doc is named after the subsystem directory (`Comms/` →
`SHIMMER3_BT_COMMUNICATION_PROTOCOL.md`, and so on) — see that repo's `AGENTS.md` for the full
mapping. Scope it to behaviour, not renames or refactors.

A doc change in the submodule is its own PR plus a pointer bump here, so allow for two PRs when the
change spans both.
