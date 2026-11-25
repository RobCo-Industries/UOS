# Changelog

All notable changes to UOS are recorded here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Changed

- Rebuilt UOS on RobCo-Core. The prototype is gone.

### Added

- The screen stack redraws what a closing screen uncovers. A pop, a replace or `uos_screen_expose()` draws every visible screen bottom-up from the highest full screen. BACK from the holotape reader or a pager over the login prompt now gives the whole login screen back.

- The password game plays only from the login prompt. `hack start` over another screen refuses with `covered`, and BACK gives that screen back as it was.

- Arrival order on the maintenance port: a call that a command posts goes through the input queue, so it runs after the keys received before it and before later ones. `hack seed` now posts too. Port calls count in `pending=` until they run.

- The build: `UOS.SYS`, a sealed rimg linked at `0x00400000`, with host and QEMU tests and CI on four boards.
- The kernel entry with the trust rule and one marker per bring-up step.
- The panic path for `uos_panic()` and CPU exceptions.
- The event loop with an event queue, timers in ticks and a stack of screens.
- The `UOS.CFG` reader with clamps, defaults and counts of bad values.
- The maintenance port: `state`, `dump`, `type`, `timers` and `reboot`.
- A first screen with the boot banner and a placeholder for the login.
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
- The session service and the login screen: `SYSUAF.DAT` loaded at boot, `USERNAME:` and `PASSWORD:` prompts from the serial console or the on-screen keyboard, a password check in steps, one message for any failed login, the lockout screen with its countdown, a welcome, a placeholder screen after login and logout. `src/session/sess.h` for the next tasks.
- A system screen that names a missing or damaged user database, no card or no `/UOS` folder, and restarts on F4.
- `[login] idle_s` in `UOS.CFG`: a session ends after that many seconds with no key. 0, the default, means never.
- Maintenance: `sess state`, `sess logout`; in test builds `sess login <user>` and `button <key>`.
- A console read, pager or pause popped by another caller ends without calling back.
- `docs/USER_MANUAL.md` with a "Logging in" section.
- The panic path starts the maintenance port when a panic comes before it, with the base commands in the registry.
- `uptime` reports `last=`, the tick of its last service tick.
- The `vfs` service: file specifications to card files with access checks, safe writes, listings, wildcards, stepped copies, a `DEV:` hook and the `fs` maintenance command.
- The console service: an event-driven line editor with history and masked input, the on-screen keyboard on a line read, a pager screen, and shared output helpers.
- The `devices` service: `DEVICES.CFG` on the GPIO pins, the `dev` maintenance command, safe outputs at shutdown and a software watchdog.
- The services work together: `DEV:` names reach the device service through the file system, the device service reads `DEVICES.CFG` through it, and shutdown stops the devices first.
- The console echoes the line being read on the PL011, with `*` for a masked line.
- File system errors have their own messages: `WRITLCK`, `PROTECT`, `TOOBIG` and `INSFMEM`.
- Devices: a failed write to an output's safe level is retried every tick and shown as a fault, so the status never says a state the pin does not have.
- The `fs copy <from> <to> new` maintenance form, which refuses an existing target.
- The holotape reader screen: scans `SYS$HOLO`, lists the tapes with damaged ones marked, reads entries in the pager, frees the tape on close, and says so for no card, no folder, no tapes and no memory. `holo_screen_start()` and the `holo` maintenance command.
- The password game screen: F2 on an empty user name at the login starts it. Arrow keys or buttons move, SELECT guesses, bracket pairs remove a dud or reset the attempts. A win logs in the `[hack] user`; a loss locks the terminal with the login's lockout screen. `hack_screen_start()` and the `hack` maintenance command, with a test-only fixed seed.
- `holo pick`, `hack move`, `hack select` and `hack back` carry their arguments and the screen they were meant for in the posted call. A call for a closed screen marks `result=gone` and does nothing.
- The session service: `sess_hack_check()`, `sess_hack_begin()`, `sess_hack_lock()`, `sess_set_hack_entry()` and `sess_resume()`; `lk_lock()` for a lock of a given length; the `lockout` markers say what locked.
- `CON_RL_ALTKEY` for `con_read_line()`: F2 on an empty line ends the read with `CON_ALT`.

### Fixed

- `uos_dec_u32()` refused nothing above a limit below 9 for a one-digit text: `max - digit` wrapped. The file system commands' copy of the parser had the same fault and now use it.
- The holotape reader marked a tape damaged when the scan could not read it, for lack of memory for example. Such a tape now shows `NOT CHECKED` and opens when picked.
- The password game printed a refusal twice on the serial line. The holotape reader printed its load results on the screen only; they now reach the serial line once.
- Test builds: two `sess login` commands in one port write both used the second name. Each now carries its own.
- The `hack`, `holo` and `sess` state replies could say `pending=0` while keys waited in the loop's queue. They use `uos_input_pending()`.
- Devices: `ARM` stays off at boot when any other output failed its initial write, an expander output with no driver included. The status and the answer to arming by hand say how many outputs are unreachable.
- Files: every call sees a file cut mid-write as recovery leaves it. A copy finds a source held only in a work file, and a new-only copy or write sees the restored target. Listings show the real name, never a work file.
- Files: on a read-only card, reads and listings find the content a work file holds, and writes fail as read-only instead of "not found".
- The loop runs only the events queued before each pass, so a stepped copy no longer holds off input and the tick until it ends.
- Files: a multi-file `fs copy` keeps the user, privileges and default of its start for every file. `fs who` and `fs default` answer `err busy` while a request runs.
- Files: `fs free` prints the full 64-bit total and free bytes, so a card over 4 GiB reports its real size.
- Console: a `readline` refused as busy no longer changes the flags of the running read, so a masked line can no longer show in its marker.
- `vp_access()` no longer reads a home folder name past the depth of a file directly in `HOME`.
