# Changelog

All notable changes to UOS are recorded here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Changed

- Rebuilt UOS on RobCo-Core. The prototype is gone.
- `AUTHORIZE`'s `/PASSWORD` takes no value: the command asks for the password, masked.
- The shell replaces the placeholder screen after login. The placeholder is gone: with no screen registered, the session ends with `session screen=none`.
- The shell screen sets `full`, so the stack redraws it from the shell up when a screen over it closes.
- `/OUTPUT` replaces its file only when the command succeeds. A failure, a warning or Ctrl-C leaves the old file as it was.
- A wildcard whose matches the access table all refused gives `%UOS-E-NOPRIV`, not `%UOS-W-NOFILES`. Some refused give `%UOS-I-SKIPPED` and `skipped=` in the end marker. `vfs_list_keep()` returns `VFS_KEEP_REFUSED` for them.
- `sh_program_register()`: `start(args, done, tag)` and `done(tag, status, text)`. A late `done()` from a cancelled run is ignored.
- Ctrl-C ends a running command as cancelled whatever screen is on top: the `TYPE /PAGE` pager, a `/CONFIRM` question, a password, a `CREATE` line, the on-screen keyboard over a read, and a program's own screen. Before, Ctrl-C in the pager did nothing and at `/CONFIRM` it reported the files done as a success.
- The pager calls back with `CON_CANCEL` on Ctrl-C; `page` marks `ev=page result=cancel`.
- A program's `done()` takes `SH_CANCELLED` for Ctrl-C on its screen.
- `sh_run_line()` refuses an empty or comment-only line with `SH_ERR_ARG` and no callback, and `sh run` refuses it with `rc=-1`. The prompt still shows a new prompt for one.
- `sh_program_register()` returns `SH_ERR_DOWN` before the shell service started.
- `TYPE` and `HELP` print 8 display rows a step, wrapped segments of a long line included, so a 1 MiB line no longer holds the loop.
- `DEVICES.CFG` and `TERMINAL.CFG` read with `ini_parse_sections()`: an empty section is `DV_ERR_EMPTY` or `TUI_E_EMPTY`, a repeated one is a counted error even right after itself, and section errors carry the header's line.
- A repeated `[device NAME]`, `[expander N]` or `[entry N]` header is refused even when the first section was empty or bad. `TERMINAL.CFG` remembers every entry number, not the first 32.
- The `ARM` rule: an expander output needs a relay named `ARM` on a Pi pin that is off at reset. Without one, each expander output is refused with `%DEVICE-E-NOARM`, shows ` REFUSED=NOARM` and is never driven.
- RobCo-Core moved to `4249632`. The library's two-pass `test-host-gccwarn` and its `make ci-local` replace UOS's own `test-host-gccwarn-ci` and `tools/ci_local.sh`.

### Added

- A covered screen takes no posted action. `uos_screen_pop(s)` pops `s` only when it is the top, and refuses any other pop with `UOS_SCREEN_ERR_TOP` and a `screen pop=refused` marker; a test build panics. `uos_screen_on_top()` and `uos_screen_unwind()` are new. `hack move`, `select` and `back`, `holo pick`, `osk open` and `close`, and `sh run` reply `covered` under another screen. Before, `hack back` under the holotape reader popped the reader and left the game open, `holo pick` could open a pager over another screen, and `sh run` popped the reader over the shell. The game's result pause waits until the game is the top again.
- Held keys follow one rule on every screen: movement and typed characters repeat, an action that confirms, cancels, loads or leaves takes only the press. `uos_ev_fresh_key()` holds the rule, and the screen stack drops the repeats of a key whose press changed the stack. Before, a held BACK in the holotape reader left it, a held SELECT could start a second action, and a held BACK on the session placeholder, the pager, the welcome or a pause ran on into the next screen. The test-only `hold` command sends a press, repeats and a release.
- A won password game clears the count of failed logins, as a right password does. Before, failures from before the game carried over to the next login. `lk_success()` is now `lk_session_start()`, called by `sess_begin()`. `docs/modules/session.md` has the table of every lockout event.
- The screen stack redraws what a closing screen uncovers. A pop, a replace or `uos_screen_expose()` draws every visible screen bottom-up from the highest full screen. BACK from the holotape reader or a pager over the login prompt now gives the whole login screen back.
- The password game plays only from the login prompt. `hack start` over another screen refuses with `covered`, and BACK gives that screen back as it was.
- Arrival order on the maintenance port: a call that a command posts goes through the input queue, so it runs after the keys received before it and before later ones. `hack seed` now posts too. Port calls count in `pending=` until they run.
- Expander pins work: the device service drives PCA9554 chips through the library's `gpiox.h`. A chip that does not answer at boot leaves its devices unreachable and `ARM` off. On panic and from the software watchdog no I2C transfer starts. The pin record names the chip.
- An optional hardware watchdog, `UOS.CFG [watchdog]`, off by default. The loop's tick kicks it; a panic resets the board after the outputs went safe; a halt stops it. `dev hwdog` shows it.
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
- The command shell after login: a DCL prompt with continuation lines, prompts for missing parameters, privilege checks and system messages. Commands run in steps and Ctrl-C stops them. `docs/COMMANDS.md` describes every verb.
- Commands: `DIRECTORY`, `TYPE`, `COPY`, `DELETE`, `RENAME`, `CREATE`, `SET DEFAULT`, `SET TERMINAL`, `SET HALT`, `SET PASSWORD`, every `SHOW` keyword, `HELP`, `RUN`, `LOGIN`, `LOGOUT`, `ROBCO`, `DEVICE` and `AUTHORIZE`. `HOLOTAPE`, `MAIL` and the `RUN` programs say they are not installed.
- `src/shell/sh.h`: `sh_run_line()` runs a command line for another screen, and `sh_program_register()` adds a program for `RUN`.
- `SET PASSWORD` and `AUTHORIZE` write `SYSUAF.DAT` with masked password reads, salts from the RNG and an atomic write; `sess_reload()` reads it again.
- Help files for every verb in `/UOS/SYS/HELP` on the default card.
- Maintenance: `sh state` and `sh run <line>`; `cmd` and `sh` markers.
- `docs/USER_MANUAL.md`: a "Using the command line" section.

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
