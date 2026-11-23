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
- The `devices` service: `DEVICES.CFG` on the GPIO pins, the `dev` maintenance command, safe outputs at shutdown and a software watchdog.
- Eight pure modules with host tests, not yet wired into the system: `dcl`, `vfs` path, `session`, `osk`, `dev`, `tui`, `hack` and `holo`.
- One set of privilege bits for every module, in `src/kernel/uos_privs.h`.
- Devices: the refused pin mask is a parameter, UOS frees the dial's pins, up to 24 devices, a warning for an active-high output on a pin pulled up at reset, and expander pins as `X<chip>.<pin>` with `[expander N]` sections.
- Card tools with pytest tests: `mkuaf.py`, `mkwords.py`, `mkholo.py`. `make test-tools` runs them.
- The default card's user database, devices, menus, word lists and holotapes.
- `docs/FORMATS.md` and a reference page per module in `docs/modules/`.
- A registry for maintenance commands, `uos_maint_register()`, with `help` and quoted words. Every base command moved onto it.
- A table of services in `src/kernel/services.c` with `ev=service` and `ev=shutdown` markers, and the first service, `uptime`.
- `uos_halt()`, and `uos_reboot()` that shuts the services down first. The panic path shuts them down too.
- The MMU and the caches are on from early boot, with an `ev=mmu` marker.
- Test builds: `halt`, `bench uaf <n>` and a failing service for the tests.
- The panic path starts the maintenance port when a panic comes before it, with the base commands in the registry.
- `uptime` reports `last=`, the tick of its last service tick.
- The `vfs` service: file specifications to card files with access checks, safe writes, listings, wildcards, stepped copies, a `DEV:` hook and the `fs` maintenance command.
- The console service: an event-driven line editor with history and masked input, the on-screen keyboard on a line read, a pager screen, and shared output helpers.
