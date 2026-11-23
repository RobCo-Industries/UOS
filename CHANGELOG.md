# Changelog

All notable changes to UOS are recorded here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Changed

- Rebuilt UOS on RobCo-Core. The prototype is gone.

### Added

- The build: `UOS.SYS`, a sealed rimg linked at `0x00400000`, with host and QEMU tests and CI on four boards.
- The kernel entry with the trust rule and one marker per bring-up step.
- The panic path for `uos_panic()` and CPU exceptions.
- The event loop with an event queue, timers in ticks and a stack of screens.
- The `UOS.CFG` reader with clamps, defaults and counts of bad values.
- The maintenance port: `state`, `dump`, `type`, `timers` and `reboot`.
- A first screen with the boot banner and a placeholder for the login.
