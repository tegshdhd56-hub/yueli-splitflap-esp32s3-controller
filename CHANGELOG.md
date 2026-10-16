# Changelog

## Unreleased — open-source preparation

- Identifies the public development tree as `R10.1-dev` and rotates the browser
  asset cache token so it cannot reuse immutable R10 assets.
- Moved deployment credentials into ignored `LocalSecrets.h` configuration.
- Added build, flashing, API, extension and security documentation.
- Selected the Apache License 2.0 and added the YUELI attribution notice.
- Added the official `https://www.yueli.hk/` identity to firmware constants,
  startup diagnostics, JSON status/settings responses and the device UI footer.
- Withheld historical R10 archives from the public repository because of legacy
  shared credentials, absent hardware validation and unresolved third-party
  redistribution obligations.

## R10 — 2026-10-15

- Increased the startup quiet period to 20 seconds.
- Added a homepage countdown synchronized to device startup state.
- After the quiet period, sends per-unit blank-page commands for DIP 0-137 and a final blank broadcast.
- Applied the display-update gate to TCP writes during startup and calibration locks.
- Preserved text editing, Wi-Fi provisioning, carousel playback and Developer calibration from R9.

### Verification status

- Source inputs matched the recorded R10 source hashes.
- Firmware images and partitions passed recorded static validation.
- The release package was not flashed to or tested on physical hardware when created.
