# Changelog

All notable changes to UOS are recorded here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added

- Event sounds. With `[audio] sound = on` a good password plays `pass_good` and a bad one `pass_bad`, at the login and in the password game. A bracket in the game plays a key sound. A cursor move in the game, the menu and the pager plays `char_scroll`, at most once per 30 ms so a held arrow key does not smear. Each event plays one sound. With sound off nothing changes.
- Key sounds. With `[audio] sound = on` each key the line editor takes plays one of two key sounds, a typed character and Enter included. All sounds are original synthesis from the parameters in `src/sfx/sfx_models.json`, rendered by the library's `tools/sfx_table.py` into the committed `src/sfx/sfx_table.c` and `.h`. `make sfx-table` writes them and `make sfx-table-check`, which every image build runs, `make sdimage` and `make run` included, stops a stale table. A new key `[audio] trace = on|off`, default `off`, prints the audio trace lines; off, UOS drops them so typing over serial is not flooded. The `config` marker ends with `trace=`. No effect sound plays before the rest ramp has ended, so an early key cannot cut it and click. `UOS_MARK_MAX` grows from 448 to 544 bytes. `UOS.SYS` grows from 297 744 to 404 732 bytes, the six tables.
- `UOS.CFG [audio]` opens the sound path: `sound = on|off`, default `off`, `fan = on|off`, default `off`, and `volume = 0..100`, default 80. With `sound = on` the new `sound` service starts the audio driver, plays the midpoint rest ramp, polls it in the loop and stops it at shutdown. Nothing is heard yet. `fan` is read and kept for later. A new `sound` boot marker gives `result=on`, `off` or `error` with the library's code, and the `config` marker now ends with `sound=`, `fan=` and `volume=`.
- `docs/ADMIN_GUIDE.md` for the person who prepares the card, `docs/QUICK_REFERENCE.md`, and `docs/LORE.md`, which sets the in-world documents against the real system. Writing a menu moved from the user manual to the administrator guide.
- `RUN HOLOTAPE` and the `HOLOTAPE` verb open the holotape reader; `HOLOTAPE <tape>` opens one tape. `RUN HACK` opens the password game's screen, which from a session shows the `busy` refusal. Both end the command at once, as `RUN TERMINAL` does.
- `LOGIN <user>` ends the session and asks only for that user's password, with the login's retries and lockout.
- UOS starts six GPIO buttons at boot: UP 5, DOWN 6, LEFT 17, RIGHT 22, SELECT 26 and BACK 27, active low with pull-ups and a 20 ms debounce. `UOS.CFG [input] buttons = off` turns them off. A new `buttons` boot step after `config` prints the result, and `SHOW SYSTEM` shows it. When every line reads pressed at boot, as in QEMU, UOS turns the buttons off until the next boot. The `input` marker now ends with `gpio=later`, and the `config` marker ends with `buttons=`. The six pins stay refused in `DEVICES.CFG` whatever the setting.

### Fixed

- The lore documents no longer claim a credential dump, MD5 passwords, a `MAIL` system or a network. They say they are fiction and link to the real docs.
- A terminal with panel buttons and no keyboard can now log in and start the password game. At an empty `USERNAME:` with the on-screen keyboard closed, the BACK button starts the game, as F2 does. Any other button opens the keyboard. A BACK from the serial line does not start it. The user manual says so. The session page no longer says `SECURITY` skips the password game's lockout.
- A logout or `LOGIN <user>` queued right after `RUN HACK`, `RUN HOLOTAPE` or `RUN TERMINAL` no longer opens that screen over the login prompt. The password game no longer starts there for real. The dropped call prints `sh stale=post`.
- `HOLOTAPE ABCDEFGH.HTPX` no longer opens `ABCDEFGH.HTP`. A typed tape name is used whole or refused: 1 to 8 of `A-Z 0-9 $ _ -`, then nothing or `.HTP`, else `%UOS-E-IVSPEC`. A menu's `holotape` target follows the same rule.

- An unknown key or a key given twice in a `[device NAME]` or `[expander N]` section of `DEVICES.CFG` now refuses that section with `BADKEY` or `DUPKEY`. Before, the device loaded with a default nobody chose, so `actve = low` drove the opposite level. The login screen shows `DEVICES: SOME DEVICES REFUSED` and `SHOW DEVICES` ends with `%DEVICE-W-CFGERR` when the file has errors or cannot be read. The line gives the count, says when the serial line names only the first 8, and says when the file cannot be read.
- A `TERMINAL.CFG` that cannot be read, does not parse or has no entry for the user no longer opens the command line at login for a user without `SYSTEM` or `SECURITY`. That user gets the menu screen with the error and can only log out. A file whose valid entries are all hidden shows `No menu entry for this user in TERMINAL.CFG`. A file whose entries are all bad shows the first parser error. A file with no entry shows `No menu entry in TERMINAL.CFG`. A user with `SYSTEM` or `SECURITY` gets the command line with `%UOS-W-BADMENU` under the banner. The `tui op=check` marker gains `why=` and `line=`.
- `AUTHORIZE` no longer lets a `REMOVE`, `/PRIVS`, `/NOPRIVS` or `/DISABLED` leave no enabled user with `SYSTEM` or `SECURITY`. It ends with `%UAF-E-LASTADMIN`, prints `uaf result=refused why=lastadmin`, and writes nothing. `REMOVE`, `ADD` and `MODIFY /HOME=` now say that no home folder is deleted or moved: `%UAF-I-HOMEKEPT`, `%UAF-I-HOMEUSED` and `%UAF-I-NOMOVE`. A file in place of the home folder is not taken as the home: `ADD` and `MODIFY /HOME=` report `%UOS-E-EXISTS`.

### Removed

- The `[system] mode` key. Nothing acted on it. A card that still has it counts one unknown key, and the `config` marker has no `mode=` field. The login screen now shows `SETTINGS: SOME VALUES IGNORED` on its last header row, so the notice no longer vanishes when the login replaces the first screen. The serial line gets it too, from the first login on.
- The `MAIL` verb, its keywords and `SYS$HELP:MAIL.HLP`. It was never installed.
- The `HOLOTAPE` verb's second parameter, an entry, which the reader cannot open.

### Changed

- RobCo-Core moved to `815e1fc`. Its audio driver now plays samples up to 37500 Hz, rests the pin at the midpoint with `audio_rest_mid()`, plays one overlay over a play, and centres tones after that ramp.
- The card tools fit what UOS does. `mkuaf.py build` notes a name or home it folds to upper case, and a file with no enabled `SYSTEM` or `SECURITY` user, and still exits 0. `mkholo.py` finds `rimg.py` in the checkout when neither `--rimg` nor `ROBCO_CORE` is given. A `--rimg` path or a `ROBCO_CORE` folder without `rimg.py` now stops it with exit code 3 instead of falling back. `mkwords.py check` fails only on what UOS refuses, now including a list file over 7232 bytes, and prints the rest as notes; `check --strict` keeps the old rules. The holotape sources moved from `card/UOS/HOLO/SRC/` to `tools/holotapes/`, so `make sdimage` no longer copies them to the card. The `.HTP` files are unchanged.
- A card taken out while UOS runs gives `%UOS-E-NODEV`, not a read or write error. `vfs_from_fs()` maps the library's new `FS_ERR_GONE` to `VFS_E_NOCARD`, and a host test fails for any library error code with no mapping.
- Only OPER has a `TERMINAL.CFG` on the default card, with one entry of each type. ADMIN's and GUEST's menus and `ADMIN/MAINT.CFG` are gone, so they get the shell.

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
- RobCo-Core moved to `ef5079f`. It brings removable volumes in `fs.h`, the holotape slot driver, GPIO input in QEMU tests, a fixed dial decoder, a harness that leaves no QEMU and no passing test's temp data behind, and the maintenance request spec. UOS sets `ROBCO_FS_VOLUMES` to 1: it has one card. `UOS.SYS` grows by 336 bytes, the removal checks, and BSS shrinks by 48 bytes.
- RobCo-Core moved to `4249632`. The library's two-pass `test-host-gccwarn` and its `make ci-local` replace UOS's own `test-host-gccwarn-ci` and `tools/ci_local.sh`.

### Added

- The terminal menu screen, `src/tui/tuiscr.c`, and its `menu` service. A user whose home holds `TERMINAL.CFG` gets it after the login; others get the shell. Every entry type works: text in the pager, sub-menus to 8 deep, commands with their output in the pager, devices, holotapes, logout. Each failure shows one line and leaves the menu usable. Frames are drawn with lines. Markers `ev=tui`, command `menu state|msg|show|list|pick|back`.
- `RUN TERMINAL` opens the menu over the shell. BACK at the top closes it.
- `sh_run_line_on()`, `sh_host_uncover()`, `sh_host_gone()`, `sh_cancel()`, `SH_RUN_CAPTURE` with `sh_output()`, and `sh_session_screen_register()` in `sh.h`.
- `holo_screen_start_tape()` opens the holotape reader on a named tape.
- `tui_set_close()`, `tui_map_key()`, `tui_act_name()`, `tui_state_name()` and `TUI_ACT_CLOSE` in the menu model.

- `SET HALT /MAINTENANCE` writes the maintenance request `/ROBCO/MAINT.REQ`, then restarts with `RESTART` or halts without it. A failed write ends with `%UOS-E-NOMAINT` and the reason, and nothing halts. `/NOMAINTENANCE` removes a request. Marker `ev=maint_req`.
- `vfs_maint_request()` and `vfs_maint_clear()`, the only writes UOS makes in the boot tree.
- The card ships `/UOS/SYSUAF.DEF`, the default user database for the maintenance image's restore users tool. Nobody may change it: every write, delete, copy or rename onto or over it is `%UOS-E-PROTECT`. Reading needs `SYSTEM` or `SECURITY`, as for `SYSUAF.DAT`.
- A covered screen takes no posted action. `uos_screen_pop(s)` pops `s` only when it is the top, and refuses any other pop with `UOS_SCREEN_ERR_TOP` and a `screen pop=refused` marker; a test build panics. `uos_screen_on_top()` and `uos_screen_unwind()` are new. `hack move`, `select` and `back`, `holo pick`, `osk open` and `close`, and `sh run` reply `covered` under another screen. Before, `hack back` under the holotape reader popped the reader and left the game open, `holo pick` could open a pager over another screen, and `sh run` popped the reader over the shell. The game's result pause waits until the game is the top again. A posted step that finds its screen covered no longer drops its work: a screen's new `uncover()` callback runs once after a pop leaves it on top, and the login, the shell, the game and the banner use it to start the read or the end they owe. Before, an empty user name typed as the reader opened left the login with no prompt, and every key was lost; the shell had the same fault after an empty line. A shell command now waits while another screen covers the shell: its next step or its end runs once the shell is on top, through `uncover()`. Before, a `/CONFIRM` question or the `TYPE /PAGE` pager opened over the holotape reader, and a program's result line painted over it. `sh_run_line()` returns the new `SH_ERR_COVERED` under another screen, so no command, question or program starts there. A screen now pushes its read line, pager or pause as its child, and the owner and the child form one unit. When a pop leaves a child on top, the stack calls `uncover()` for the child and then for each owner below it, once each. Before, a command the API started under the `$` read stalled when the holotape reader opened and closed over that read, until the user ended the prompt. `uos_screen_unit_on_top()` is the one "covered" test for a unit, and `con_read_line()`, `con_page_text()` and `con_pause()` take the owner as their first argument. The new `screens` maintenance command lists the stack. A timer or a posted step now paints its screen only through the stack, with the new `uos_screen_dirty()`: on top it redraws, covered it waits for the pop. Before, the banner's end drew the first screen over a pager opened during the banner. A session reset that takes the shell away under a running job leaves the job an orphan that takes no step and ends with `CANCEL` on the next tick. Before, the step the job had posted ran after `sess logout` and printed over the login screen. The shell takes a job only when its screen is on the stack and on top: during the welcome `sh_run_line()` returns `SH_ERR_COVERED` and `sh run` marks `why=covered`. Before, a job started then ran and printed over the welcome. The build strips the project folder from the front of `BUILD`: the test fixtures pass an absolute path, `make all` a relative one, and the `.d` files of one never matched the other's targets, so a header change could leave a stale object in a test image.
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

- A lockout started by the password game prints its own start tick in `lockout state=start`. A failure during a running lock no longer moves that tick: `lk_fail()` and `lk_lock()` return `LK_STARTED` only for a new lock.
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
