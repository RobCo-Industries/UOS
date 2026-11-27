# Testing UOS

Every test runs on the host or in QEMU. Nothing is validated on real hardware.

## Host tests

`make test-host` builds each `tests/host/test_*.c` with the host compiler, `-Werror` and the address and undefined behaviour sanitizers. Each test links every pure file. It also compiles the tests and the pure files with the cross GCC, to catch warnings only GCC reports. `make all` compiles every module source with the target flags and `-Werror`.

### The cross GCC check

On a Mac the host compiler is clang, and CI's host compiler is GCC. GCC reports warnings clang does not, such as `-Wformat-truncation`. The library's `test-host-gccwarn`, from `mk/host.mk`, compiles every C file in `tests/host/` and every pure file with the cross GCC and the macOS SDK headers to catch them before a push. `make test-host` runs it. Each file goes through two passes:

- The ci pass uses CI's host flags: `-O1`, `-Wall -Wextra -Werror` and the sanitizers. UBSan's checks hide loop bounds from GCC, so some truncation warnings appear only with these flags.
- The o2 pass drops the sanitizers and uses `-O2`, which other GCC warnings need.

Each file and pass leaves a stamp in `build/host/gccwarn`, so a second run compiles only what changed. The check skips with one line when the host compiler is GCC, the cross GCC is missing or no host headers are found. Set `HOST_GCCWARN_INC` to the headers if needed. The macOS headers are not glibc's, so a few results can differ from CI, for example the size of `struct dirent`'s `d_name`.

### The CI build in a container

`make ci-local` runs the CI build job, `make CI=1 all test-host`, in the robco-ci image. It comes from the library's `mk/host.mk` and `tools/ci_local.sh`. The Makefile sets `CI_LOCAL_TARGETS` to the workflow's `make-targets`. The script copies the tree to `build/ci-local/src` and runs the container as your user, with no host environment and at most `CI_LOCAL_CPUS` CPUs, 4 by default. It needs Docker.

The image is `ghcr.io/robco-industries/robco-ci:<tag>`, with the tag from `external/robco-core/ci/IMAGE_TAG`. Pulling it needs a GitHub token with `read:packages`. Without one, build it locally once with `make ci-local-image`. `ci-local` uses the local tag when it exists.

To run other targets, set `CI_LOCAL_TARGETS` for one run: `make ci-local CI_LOCAL_TARGETS="-k all test-host"` or `make ci-local CI_LOCAL_TARGETS="test-target BOARD=raspi0"`.

### Test files

The tests run from the repository root, so the ones that read the default card open `card/UOS/...` by a relative path. Before they build, `tools/mkuaf.py vectors-h` writes `build/host/gen/uaf_vectors.h` from `tests/vectors/uaf.json`.

| File | Covers |
|---|---|
| `test_portcall.c` | Port calls in the input queue: a key then a call, a call then a key, mixed sequences taken in steps of 1 to 8 events, all in arrival order. A full slot table and a full input queue refuse; releases, dials and stale slots are dropped. |
| `test_timerq.c` | Timers: one-shot, periodic, catch-up, the tick wrap, cancel before and after expiry, limits. Delivery through a full event queue: a one-shot, a periodic timer over several periods, a cancel while pending, 16 timers due, a timer started from a callback. The event queue. `uos_ev_fresh_key()`, the held-key rule. |
| `test_screen.c` | The screen stack rules: push, pop, replace, events passed down, draws. The redraw order: after a pop, a replace or an expose, every visible screen from the highest full one up; after a push or an invalidate, the top only; from the bottom when no screen is full. A held key that closed a screen: its repeats stop there, other keys pass, a new press ends the drop. |
| `test_settings.c` | `UOS.CFG`: every key, `[login] idle_s`, `[input] buttons` and the `[watchdog]` keys included, clamps, bad values, unknown keys, syntax errors, the shipped card's watchdog off |
| `test_sfx.c` | The key pick for the first 32 keys, each table's length, CRC-32 and peak against the generated header, and the loudest one-shot plus the loudest loop within 127 steps. The rest gate: it waits for the ramp, retries a cut ramp once, then stays shut |
| `test_buttons.c` | The six button pins and codes in order, their mask equal to the device service's button pins and clear of the fixed pins, the `buttons` result for each case of the setting, the library code and the guard's status, every result name |
| `test_hwdog.c` | The hardware watchdog's rules: off unless enabled and in range, a kick every quarter of the timeout, across the tick wrap, none from a dead loop, none after a shutdown, halt stops it and panic leaves it |
| `test_hack_ui.c` | The password game's screen model: cell positions, board rows, the history at the bottom, the cursor and attempts lines, keys with held repeats, `[hack]` ranges, the word list path, screen states and start results |
| `test_holo_screen.c` | The holotape reader's screen model: held keys, scan and load results, notices, the loading text |
| `test_bootlines.c` | The text of the first screen, with and without the session's notice |
| `test_maintreg.c` | The maintenance command table: name rules, duplicates and library built-ins refused, sorted order, the 48-command limit; the word splitting with quotes, the word and line limits; joining the channel's words back |
| `test_service.c` | The service rules with fake services: inits in order, a failure does not stop the next, ticks only for started services, shutdown in reverse order and once, nothing before init, the table limit |
| `test_privs.c` | Every module's privilege names equal the shared bits of `uos_privs.h`; the `SYSUAF.DAT` names of each bit |
| `test_dcl.c` | The DCL tokeniser, continuation, numbers, the abbreviation rule, qualifiers and parameters, prompts, privileges |
| `test_dcl_msg.c` | The text of every system message, the file messages `WRITLCK`, `PROTECT`, `TOOBIG` and `INSFMEM` included |
| `test_dcl_table.c` | The UOS command table: every verb and keyword, the table check |
| `test_vfs_path.c` | File specifications: parse, resolve, format, logical names, wildcards, listing order, the access table; a file directly in `HOME` with a stale name past its depth |
| `test_vfs_rules.c` | The vfs service's rules: error words, the message map with its read-only, protected, too-large and memory messages, library and path codes, the date format, work names, wildcard targets, parent and entry paths, the listing filter with a refused match told apart from no match, the bounded sorted insert, the recovery pick |
| `test_vfs_svc.c` | The `fs` command of `svc.c` over `vfs.c` on a host image, with a fake loop that runs one posted call at a time: a three-file copy keeps its target and privileges when the default and the user change between steps, `fs default` and `fs who` refused while it runs, a posted request keeps the context of its post, `fs free` on a small image and on a sparse 6 GiB FAT32 image |
| `test_text.c` | `uos_dec_u32()` at limits 0, 1, 5, 9, 10 and 2^32 - 1: at the limit, one above, far above, leading zeros, empty, 20 digits. `uos_quote_word()` |
| `test_u64dec.c` | `uos_u64_dec()`: 0, 1, 2^32 - 1, 2^32, 10^12, 10^19 and 2^64 - 1, 64 values checked against libc, buffers one byte short |
| `test_vfs_card.c` | `vfs.c` over the library's FatFs glue on a host image: every entry point after an atomic write cut between its renames, on a writable card and on a read-only one, and `DEV:` left alone. The maintenance request: created with its note and no work file, an existing file kept, an empty note, no `/ROBCO` folder, a folder of that name, a read-only card and no card refused with nothing left; the clear of a file, of nothing and of a folder |
| `test_session_sha256.c` | SHA-256: the standard short and long messages, a million `a` streamed, block boundaries, length carry |
| `test_session_uaf.c` | `SYSUAF.DAT`: layout, every validation check, the hash vectors shared with `mkuaf.py`, verify with equal work, edits, privilege names, the default card's file |
| `test_session_lockout.c` | The login lockout: counts, the lock time, the tick wrap |
| `test_session_login.c` | The login rules: the stepped check gives what `uaf_verify()` gives with the same work in every case, an unknown user takes the same steps, no step passes its budget, the password copy and the hashes are wiped, an unfinished check fails; every state and event of the login state machine, ignored events, a direct session; the messages, the lockout fallback to 3 and 30, the idle rule across the wrap, the database notices, the header and welcome text, the marker name word |
| `test_osk.c` | The on-screen keyboard: both layers, moves with wrap, every key, emitted events, the row text |
| `test_dev.c` | `DEVICES.CFG`: every key and error, the refused pin masks, the pull-up warning, expander pins, empty and repeated sections with the header's line; commands, pulses, the tick wrap, status lines, the pin record, the default card's file |
| `test_dev_svc.c` | The device service's rules: debounce, `ARM` last at boot and first at shutdown, the reverse safe order, operations mapped to calls through a fake GPIO layer, expander failures, an expander bus error owed and retried, expander writes skipped on panic, the chip in the pin record, `ARM` held at boot for each kind of output failure and armed by hand with the unreachable count, pulse ends from the tick only, late and across the wrap; the device codes mapped to file system codes for `DEV:` |
| `test_tui.c` | The menu model: moves, every action, sub-menus to eight levels, scrolling, headers, privileges, the layout; close mode; the held-key rule of `tui_map_key()`; action and state names |
| `test_tui_cfg.c` | `TERMINAL.CFG`: every key, target and error; the menus of the default card: OPER's has every entry type, ADMIN and GUEST have none |
| `test_tui_pager.c` | The pager: line ends, wrapping, moves and keys, a 64 KiB text |
| `test_card_menus.c` | Each device entry in the default card's menus names a device of its `DEVICES.CFG` and a word that device takes |
| `test_shell.c` | The shell's rules: the one error map for file, `DEV:`, path and user database codes; `/CONFIRM` answers and steps; file loop counts and their final message; the marker unquoting; line splitting; uptime, date and size formats; `DIRECTORY` rows and totals; the `CREATE` end line; the `HELP` lookup and every verb's help file on the default card; the program registry; the marker verb word; display rows of a long line, with a 1 MiB line one row per call and no read past a row's width; the `/OUTPUT` write rule by severity and cancel; job tags, a late one refused; what each `SET HALT` qualifier does to the request; the request note cut to 63 bytes and the buffer, with bytes outside printable ASCII replaced |
| `test_hack.c` | Word lists, candidate picks, 1000 boards per difficulty, the one-junk gap, the cursor, brackets, the game and its lockout, the default card's lists |
| `test_holo.c` | Tape validation step by step, every error, limits, the reader model, an unchecked tape that loads |
| `test_console_lineedit.c` | The line editor: every key, insert in the middle, the 255 and per-call limits, a full line, history from empty to 17 lines, recalled edits, masked lines, the scroll window, repeats, redraw results; the exact serial bytes of each redraw, masked, scrolled and at the largest size |
| `test_console_view.c` | The console's decisions: the input row under the keyboard, when the keyboard opens, line widths, the pager status row, the byte filter, marker quoting, command words |

## Tool tests

`make test-tools` builds the host tools the tool tests call, then runs pytest over `tests/tools/` from the venv. `tests/host/holocheck.c` is such a tool: it validates tapes with the C code, so the Python and C checks can be compared.

| File | Covers |
|---|---|
| `test_bootdir.py` | `make bootdir` from stand-in files with `FIRMWARE=0`: the folder holds the names and bytes of the chain card and equals the files of the `make sdimage CHAIN=1` image made from the same inputs, plus `UOSBOOT.LST` with each file's hash. Without `RETROS_IMG` or `MFBOOT_SYS` it stops and writes nothing. On a re-run a changed user database is kept and named, an unchanged image is replaced by new bytes, a saved home file is left alone, a clash stops the run with nothing changed, `CLEAN=1` removes an unchanged stale file and keeps a changed one, `REPLACE=1` writes the shipped file back, and a first run into a folder it did not make stops |
| `test_chain_card.py` | `make sdimage CHAIN=1` from stand-in files: `MAINT_SYS`, `DIAG_SYS` and `BOOT_CFG` land in `/ROBCO` with their bytes, and without them the card keeps the one-entry `BOOT.CFG` |
| `test_mkuaf.py` | `tools/mkuaf.py`: build, check, the shared vectors, every bad input, the default card's file |
| `test_mkwords.py` | `tools/mkwords.py`: check, `check --strict`, sort and sample over good and bad lists, the default card's lists |
| `test_build_deps.py` | The image build rebuilds an object after a header it includes changes, with `BUILD` spelled relative then absolute and the other way round. It compiles one object into `build/deptest` with the cross compiler and asks `make -q`. |
| `test_sfx_table.py` | The committed sound tables: `sfx_table.py --check --full` renders them again and compares bytes, and `sfx_check.py` measures the dumped tables against the model. `make -n` shows that `make`, `make sdimage` and `make run` each run the check before they compile the table |
| `test_sfx_preview.py` | The library's `tools/audio_preview.py` as `make sfx-preview` runs it. A hand-written log with one key sound gives a WAV of the right length that is not silent. A buffer the dumped tables lack gives the warning that stops the target |
| `test_mkholo.py` | `tools/mkholo.py`: build, list, check, the `rimg.py` lookup, every bad input, agreement with `holocheck`, a card `HOLO` folder with only `.HTP` files, each shipped tape against its source |

## Target tests

`make test-target BOARD=<board>` runs pytest on raspi0, raspi1ap, raspi2b and raspi3b. The tests build the cards they need with mtools. They drive UOS through the maintenance port and match only complete serial lines. `batch()` in `tests/uos.py` sends several commands in one port write and returns their replies in order, one per command.

| File | Covers |
|---|---|
| `test_buttons.py` | The six panel buttons through real pin levels with the library's `harness.pigpio`. A plain boot reads `disabled_stuck` and gives no key. `buttons = off` with the lines high reads `off`, and a press gives no key. Lines high on a card with no user file read `active`, and each button reaches the first screen as its key: a press is held until its marker shows, then released. `SHOW SYSTEM` shows the `Buttons` line. A device on pin 16 loads and one on pin 5 is `PINUSED`, with the buttons on and off. |
| `test_chain_terminal.py` | The whole chain from a built chain card, `--chain-card` with `--retros-img` and `--mfboot-sys`: the card holds the given stage images, the first stage passes its self test and loads the agent, the agent reports the entries of the card's `BOOT.CFG` and picks UOS after its countdown, and UOS starts trusted. In one boot GUEST runs shell commands, switches `LIGHT1` and is refused `DOOR1`, ADMIN copies `NOTICE.TXT`, and OPER uses the shipped menu. Pin levels come through `harness.pigpio`. After the power-off the copy is on the card, there is no `MAINT.REQ`, and no password is on the serial lines. Two more boots play the password game on a card copy whose `[hack]` keys are set to novice with a 3 second lockout: with 9 attempts F2 at the login starts a game seeded from the hardware and guessing the words in order wins a GUEST session; with 2 attempts a lost game locks the terminal, the lockout ends and GUEST logs in. Without the card the tests are skipped |
| `test_entry.py` | Every bring-up marker in order, `gpio=later` and the `buttons` marker; the trust rule after `examples/chain1`, with a good and a broken boot info; the whole chain |
| `test_chain_maint.py` | The maintenance road on a built chain card, three boots in one VM. `ADMIN` asks for maintenance and UOS restarts. The boot agent finds the request, opens its maintenance screen with no countdown, and starts the real `MAINT.SYS`, which gets boot flags bits 1 and 3. Its `check_images` tool finds every image of `/ROBCO` good, with no write, then its `restart` tool restarts. The third boot counts down, starts UOS and `GUEST` logs in. The request is gone from the card. Each boot's checks read only that boot's serial text. Needs `--chain-card`, `--retros-img` and `--mfboot-sys`, and a card built with `MAINT_SYS`, `DIAG_SYS` and `BOOT_CFG`. |
| `test_sound.py` | `[audio]` off and on: no `ev=audio` line by default, the rest ramp first, the last tick slot, the stop at shutdown. Five characters and Enter at the login give six plays at 37500 Hz with the pinned `key_a` and `key_b` CRCs. With `trace = off` no `ev=audio` line shows and the echo stays on one line. With `trace = on` no trace line splits the echo. A key sent at the `sound` marker gives no key sound and the ramp still ends with `last=881`; in QEMU the first screen takes that key. Its cards set `trace = on` unless a test needs it off. |
| `test_sound_fan.py` | `[audio] fan = on`, read by the header's CRCs. The first play after the ramp is the fan's whole table, and a queued pass joins it about every 2.2 s. A key and a bad password each give their head as `op=pcm` and the fan's rest as `op=pcm_next`. With `fan = off` a key plays its plain table. A reboot stops the fan. |
| `test_card.py` | No card, no `/UOS` folder, no `UOS.CFG`, a syntax error, bad and clamped values, the scheme and the name |
| `test_loop.py` | Keys from the port and the serial console reach the screen; a periodic timer fires the right number of times in its bound; a cancelled timer never fires; F2 twice runs one cancel probe; the timer listing; a key flood during the banner still shows the first screen in its bound, and the services tick through it; `type` with 1, 15, 30 and 31 characters |
| `test_panic.py` | The panic path for an undefined instruction, a prefetch abort and a `uos_panic()` call; a failed tick and a fault right after the exception hook, both before the input layer starts, with `state`, `dump` and `reboot` answering; in both, `help` lists exactly the base commands, `timers` answers and `type` answers `err not ready`; `reboot`; an `nm` check that the shipped image has no test hooks |
| `test_first.py` | A golden of the first screen's text and screenshots |
| `test_services.py` | The `ev=service` markers and `services`; `uptime`; shutdown markers in reverse order on `reboot`, halt and panic; a fault after the services started: one shutdown each with `why=panic`, none again on the reboot, and `help` lists every command; a failing service in test builds; `help`, unknown commands and quotes; the `bench uaf` figure |
| `test_vfs.py` | The vfs service through `fs`: every specification form, the read limit, missing files and folders, a folder read as a file; listings with wildcards, an empty folder and more entries than the maximum; write, overwrite, must-not-exist, mkdir, rename, copy of a small and a 1 MiB file with CRCs, wildcard delete, files kept across a reboot with no work file left; cells of the access table; a full card keeps the old file; no card and no `/UOS`; the `DEV:` hook with a fake and with none; cards built with work files only, `#NAME` only, `~NAME` only and both, as copy sources and targets and for stat, new, delete, rename and mkdir; `fs default` and `fs who` refused during a three-file copy, which lands every file in its first target; `fs free` against the size and free bytes read from the card image. A card over 4 GiB is too large for a test image: `test_vfs_svc.c` covers it on the host |
| `test_console.py` | Reading a line with edit keys, history, cancel and the maximum; a masked read that stays masked after a refused second read; the on-screen keyboard typing a word over both layers and leaving the screen below intact; the pager over each test text; goldens of the open keyboard and the long text's first page; command errors and a second read refused; the exact PL011 echo of a typed and edited line and of a masked one |
| `test_dev.py` | The device service: the shipped `DEVICES.CFG`, the boot record with `ARM` last, `dev set` for every action, privileges, pulse timing, two pulses, cancel, a stalled loop, sensor debounce, bad files with an empty section, expander pins on QEMU's `pca9554` model: an output that switches and reads back and a sensor on a chip input, a missing chip that leaves its devices unreachable and `ARM` off, a chip at a wrong address left untouched, the expander output safe on reboot and left as it is on panic; the `ARM` rule: an expander output with no `ARM` refused while a Pi device and an expander sensor work, `ARM` on an expander refused, and a panic with an expander output on, then a reboot that sets it to `initial` before `ARM`; no file, no card, the safe state on reboot and panic, the software watchdog reset, the hardware watchdog off by default, and the hardware watchdog setting that resets the board at start. The tests read Pi pin levels through the library's `harness.pigpio` at boot, after each command, around a pulse, in a pin fault and after a panic. An expander read cannot tell an input from an output driven high. Pulses must end within their length plus 1 tick plus `HOST_ALLOWANCE_TICKS`, 20. |
| `test_link.py` | The services together. `DEV:` through `fs`: `cat`, `ls`, writes refused and allowed by each device's own mask, a bad word, a sensor. `DEVICES.CFG` through the file system: a folder, a file over 8 KiB, no file. A read over the pager and the pager over a read. A pulse end and a sensor change during a read. A panic with the on-screen keyboard open. The shutdown order on reboot, halt and panic, with every `devsafe` line before the console stops. A reboot during a stepped copy keeps the old file. After a panic and a halt the pin record does not change for 4 s, `STILL_S`, with a pulse running. |
| `test_holo.py` | The holotape reader. The three shipped tapes listed with their titles, each entry opened in the pager and closed, the tape freed on close. A tape with a changed byte listed as damaged and refused; a tape damaged after the scan. An empty `HOLO` folder, none, no `/UOS` and no card. 40 tapes: 32 kept, the window scrolls, UP wraps. A tape of `HF_TAPE_MAX` bytes with its scan and load times. No heap, with the test-only `holo hog`. The reader with a GUEST session, and a second open refused. Two picks in one port write, each on its own index; a pick that arrives after its reader closed. Goldens of the tape list and one entry The heap held during the scan: the tape shows as not checked, never damaged, and opens once the heap is free. A load result goes to the serial line once. Held keys through the test-only `hold`: BACK and LEFT do one step from the pager and the entry list, SELECT and RIGHT load once, DOWN and UP move a row per repeat. |
| `test_hack.py` | The password game with the test-only fixed seed. A won game from F2 at the login: each likeness checked against the password, a GUEST session with `how=hack`. A lost game with 2 attempts and `lockout_s = 3`: the login's lockout screen, the game refused while locked, the end within 3 s plus 1 tick plus `HOST_ALLOWANCE_TICKS`, and no unguessed board word on the PL011. Every bracket pair once: dud or reset, a dud turned to dots, a used pair inert. GPIO buttons move and select. Two moves in one port write; a move posted for a game that ended before it ran. The refusals: game off, `NOHACK`, no such user, a disabled account, a running session. `hack start` over the holotape reader refused with `covered`, the reader the same after BACK. A logout and an idle timeout with the reader and the refusal open. A missing and a damaged word list. `hack seed` and `holo hog` only in test builds, with `nm`; the shipped image seeds from the hardware generator. Goldens of the board at start and after two guesses Each refusal reaches the serial line once. Buttons and state lines in one write: no reply says `pending=0` with an old cursor. |
| `test_screens.py` | The redraw rule. Each test dumps the whole grid with `pending=0`, opens a screen, closes it, and compares the grid cell for cell. See [Screen redraws](#screen-redraws). A held BACK closes the pager once and leaves the typed user name below as it was. Covered screens: `sh run` under the reader and `osk open` under the reader reply `covered` and change nothing. A shell command whose step is due under the reader, held with `sh hold`, waits: a `/CONFIRM` question and the `TYPE /PAGE` pager open only once the reader closes, and the `screens` stack shows `shell,holo` meanwhile. Ctrl-C under the reader leaves the command running; at the question after the reader closes it cancels. `sh api` under the reader refuses `CREATE` and `RUN` with `-7`. A test program that ends under the reader prints its result only once the reader closes, and the grid under the reader stays as it was. A command `sh api` starts under the `$` read, with or without the keyboard open, owes its step under the reader and finishes once the reader closes, with no key: the shell and its read are one unit. `test_hack.py` and `test_holo.py` check the same rule for `hack move`, `select` and `back`, the result pause and `holo pick`. Timers under the reader: the lockout's countdown fires twice and no cell of the reader changes, and the row shows the real time left once the reader closes. A pager opened during the banner with no user database stays whole when the banner ends. Session resets under the reader: a lockout's end, the welcome's end, an idle logout and `sess logout` each leave `login,readline` or `shell,readline` and keys reach the read. `sess logout` during a `TYPE` and during a held job under the reader: the job ends `CANCEL` with no step after the logout, and the login screen stays as the stack drew it. `sh run` and `sh api` during a lockout, at the login prompt and during the welcome are refused, `-3` then `-7`, and the grid on top does not change. |
| `test_login.py` | The session service. Each default user logs in, sees the shell and logs out, by the `LOGOUT` command and by `sess logout`, twice; the session fields and `state` before and after. A wrong password and an unknown user: the same marker fields, message and steps. The lockout with `retries = 2` and `lockout_s = 3`: input refused, a countdown that only goes down, the end within 3 s plus 1 tick plus `HOST_ALLOWANCE_TICKS`, then a good login. A damaged database, with F4 restarting; a missing one, a short one, no card and no `/UOS`. Ctrl-C and BACK on an empty name, an empty name, Ctrl-C on the password. The masked echo on the PL011. A login with GPIO buttons only, through the test-only `button` command and the on-screen keyboard. `sess login` only in test builds, checked with `nm`. The idle logout with `idle_s = 2`, which a key delays. The whole serial log of a session holds no password and no salt or hash in hex. Each check runs in 8 or 16 steps of under 0.1 s. Two `sess login` commands in one write: the first opens its session, the second is refused as busy. `LOGOUT` typed at the shell, then `sess logout`, in one write: one session end, never two. `sess logout`, then BACK: the login prompt comes back. |
| `test_maint.py` | `SET HALT` and the maintenance request. `OPER` writes `/ROBCO/MAINT.REQ` and the terminal restarts; a second request finds the file and keeps it; the card image, read on the host after QEMU stops, holds `UOS OPER`. `SET HALT /MAINTENANCE` writes it and halts. `GUEST` is refused and no file appears. No `/ROBCO` folder and a folder named `MAINT.REQ` make the write fail: `NOMAINT` with the reason, no halt, no restart. `/NOMAINTENANCE` removes a request, and a plain `SET HALT` leaves it. `make sdimage` puts `SYSUAF.DEF` on the card with the bytes of `SYSUAF.DAT`. `ADMIN`, a `SYSTEM` user, cannot delete, copy over, rename or rename onto `SYSUAF.DEF`, and its bytes stay; `ADMIN` reads it and `OPER` cannot. |
| `test_shell.py` | The shell through `sh run` as each default user, with `sess login` to skip the password cost. Every `SHOW` keyword, `/OUTPUT`, `HELP`, `ROBCO`, `RUN` with no name and a bad name, `MAIL` an unknown verb, bad verbs and keywords, `SET TERMINAL`. `DIRECTORY` with each qualifier, `TYPE` with a wildcard and `/PAGE`, `CREATE` of a file and a folder and cancelled, `COPY`, `RENAME` and `DELETE` with `/LOG`, wildcards and every `/CONFIRM` answer, `SET DEFAULT`, missing files and folders. `GUEST` refused in another home, by `AUTHORIZE`, `SET HALT` and a door; `SYSUAF.DAT` kept under `DELETE *.*`. `BOOT:` as the read-only volume. A 1 MiB copy cancelled with Ctrl-C, with no target and no work file left, a second command refused meanwhile. Lines typed through the line editor with `_$` and `_File:`, Ctrl-C at a prompt, `LOGOUT`. `AUTHORIZE` `ADD`, `LIST`, `MODIFY` and `REMOVE`, a login with the new password, `SET PASSWORD`, and no password on the serial lines. `/OUTPUT` onto an existing file: a failed `DIRECTORY`, a warning, no files and Ctrl-C between two steps, held with `sh hold`, leave its bytes as they were, read back from the card image; a success writes it whole. A test program, through `sh prog`, cancelled, a second `RUN`, then the first run's late end: ignored with `sh stale=prog`, and the second run ends with its own result. Wildcards with the real `SYSUAF.DAT`: every match refused is `NOPRIV` with `skipped=1`, a mixed `DELETE` deletes the others and prints `SKIPPED`, no match is `NOFILES` with `skipped=0`. `TYPE` of a 1 MiB file of one line: 8 rows a step, the tick advances during it, the longest step stays in its bound, `MAX_STEP_TICKS`, and Ctrl-C stops it. Ctrl-C through the console keys in the pager alone and over the shell, at a `/CONFIRM` question after one file, at a password, at a `CREATE` line with the on-screen keyboard open, and a program reporting `SH_CANCELLED`: each ends `CANCEL` with the message. `sh_run_line()` through `sh api`: `NOUSER` before login, `ARG` for an empty, blank, comment-only, NULL and too long line with no callback, `BUSY` under `sh hold`, one callback per started command with its status class, `SH_RUN_QUIET` printing nothing, and the prompt taking empty and comment lines. `sh api` is absent from the shipped image. Held keys through the test-only `hold`: in the `TYPE /PAGE` pager DOWN pages per repeat and BACK leaves once, at a `/CONFIRM` question SELECT answers one question, at the prompt SELECT runs the line once. |
| `test_programs.py` | The shell's programs. `RUN HOLOTAPE` ends at once and opens the reader over the prompt, BACK brings the prompt back; `HOLOTAPE PROCS` opens that tape; an entry parameter is `MAXPARM`. `RUN HACK` from a session shows the game's `busy` refusal with no lockout, and BACK leaves the session as it was. `LOGIN GUEST` asks only the password: a wrong one counts and brings `USERNAME:` back, the right one starts GUEST's session; a name that breaks the rules is `IVNAME` and keeps the session. `RUN` with no name: `INSFPRM` through the API and `_Program:` at the prompt. `RUN BOGUS` is `IVKEYW`, `MAIL` is `IVVERB`. |
| `test_env_hygiene.py` | No test copies the host environment |
| `test_sfx_preview_log.py` | Runs only for `make sfx-preview`, which passes `--sfx-preview-log`. The suite does not collect it. See [The session's sounds](#the-sessions-sounds). |

### How the tests start UOS

- Alone: QEMU loads the image at `0x00400000`. A two-word image at `0x8000` jumps there with no boot info.
- After `examples/chain1`: the library's first stage builds a boot info and an amber console, then enters a sealed two-word image at `0x00200000`. That image jumps to UOS and keeps the registers, so UOS gets chain1's boot info.
- The whole chain: with `--retros-img` and `--mfboot-sys`, or `RETROS_IMG=` and `MFBOOT_SYS=` for make, RETROS-BIOS boots from a card that holds MFBootAgent and UOS. Without both, the test is skipped.
- A built chain card: `--chain-card <file>`, or the variable `ROBCO_CHAIN_CARD` when the option is not given, names a card from `make sdimage CHAIN=1`. The nightly run passes the card this way. The `chain_card` fixture copies it to the test's temp folder, so a test can write to the copy. Without a card, a test that takes the fixture is skipped. `after_chain(retros, board, sd, qtest=True)` also opens QEMU's test port, for pin reads.

### Test builds

`UOS_TEST_HOOKS=1` adds the `fault`, `panic`, `halt` and `bench` maintenance commands, `fs pattern` and `fs fakedev`, `sess login`, `button`, `holo hog`, `hack seed` and `hold <key> <repeats>`, and a service named `testfail` whose init returns -5, first in the table. The tests build it in `build/testhooks/`. `UOS_TEST_TICK_FAIL=1` makes `tick_init()` report a failure, in `build/tickfail/`. `UOS_TEST_EARLY_FAULT=1` runs an undefined instruction just after the exception hook, in `build/earlyfault/`. None is shipped: `test_panic.py` checks with `nm` that the shipped image has none of their symbols.

### Timing bound

The timer probe runs 10 periods of 10 ticks. It must end after at least 100 ticks and at most 100 ticks plus 1 tick of loop latency plus `HOST_ALLOWANCE_TICKS`, 20 ticks. The allowance covers a loaded test host. The count must be exact.

The key flood sends 1500 `text` lines of 32 characters in one write while the banner runs. The `ready` marker reports `banner=<ticks>` since the screen was entered. It must be at least 50 ticks and at most 50 ticks plus 1 tick of loop latency plus `HOST_ALLOWANCE_TICKS`. Once the port has answered the whole flood, `uptime` must show a service tick at least 50 ticks after the services started.

### The hash bench

`test_bench_uaf` runs `bench uaf 2000` in a test build and prints a line `bench board=<b> mmu=<on|off> iterations=2000 ticks=<t> us=<us>` with `-s`. It asserts only that the hash takes at most 50 ticks, 0.5 s. QEMU takes about 1 tick. The bound is about 40 times that: a loaded host fits, a hang or a 100-times slowdown does not.

Measured in QEMU on the test host, two runs per board, 2000 iterations:

| Board | MMU on | MMU off |
|---|---|---|
| `raspi0` | 11.3 and 11.4 ms | 10.6 and 10.6 ms |
| `raspi1ap` | 11.1 and 11.3 ms | 10.6 and 10.7 ms |
| `raspi2b` | 11.5 and 11.8 ms | 10.6 and 10.9 ms |
| `raspi3b` | 10.0 and 12.0 ms | 11.6 and 11.7 ms |

The MMU off figures come from a local build with `mmu_enable()` left out. QEMU does not model caches, so the two columns are the same within noise. These figures say nothing about a real Pi, where the caches should matter much more. Nothing is validated on real hardware.

### The login check

`test_login.py -s` prints one line per login, `login board= user= len= steps= ticks= us= maxstep_us=`. The figures are in [session](modules/session.md#qemu-numbers): 7 to 18 ms per check in QEMU, 0.9 ms at most per step. The test asserts the step count, 8 or 16, and a step under `STEP_BOUND_US`, 0.1 s: about 100 times the longest step seen, so a loaded host fits and a check in one step of a real Pi without caches does not.

The tests read the session state with `sess state` and wait for `pending=0` first, so the keys sent through the port have reached the screen.

### Pending input and the state replies

A maintenance command runs inside `input_poll()`. The loop takes input events into its own queue before it hands them to a screen, so the input layer can be empty while keys still wait. The `hack`, `holo` and `sess` state replies count those keys through `uos_input_pending()`. `test_buttons_move_and_select` failed about one run in four on raspi3b before this: the state said `pending=0` with the cursor short by exactly the keys still queued. `test_state_counts_keys_the_loop_holds` sends buttons and state lines in one write and checks every reply.

A call posted by a maintenance command used to go straight into the loop's queue, ahead of keys still in the input layer. `test_pick_after_the_reader_closed` failed about half the time on raspi3b for that reason. Now the call goes through the input queue. Ordering tests send a key and an acting command in one write, in both orders: `test_pick_then_back_in_one_write` in `test_holo.py`, `test_key_and_command_keep_their_order` in `test_hack.py` and `test_key_and_logout_keep_their_order` in `test_login.py`. Only the key-first order could go wrong before; the other order guards against a call put behind later keys. The key-first tests send `FILLER` from `tests/uos.py` before the key: 20 characters the screens ignore. The loop takes one input event per poll, so the key is still in the input layer when the command comes, and the old code failed these tests on every run, not only on a loaded host.

### The shell's tests

`test_shell.py` drives the shell through `sh run`, which takes the line quoted as the console quotes markers, and waits for `sh run=done` and `cmd state=end`. It reads `sh state` and waits for `pending=0` before it trusts the state. A test answers a command's question, such as `/CONFIRM` or a password, after the `sh state=prompt kind=<kind>` marker and `ask=<kind>` in `sh state`.

QEMU refuses a read-only SD card, see below, so the shell's read-only test uses `BOOT:`, the volume the path module makes read-only. The read-only card itself is covered on the host in `test_vfs_card.c`.

The test card has no clock chip, so `SHOW TIME` is tested on its warning path only.

### The terminal menu's tests

`test_menu.py` drives the menu through `menu pick`, `menu back`, `key`, `button`, `text` and `hold`, on a test card that gives OPER and ADMIN one menu with an entry of each type, one entry for SECURITY only, and an entry for each failure. It reads `menu state` and waits for `pending=0`; `menu msg` gives the message row as it is. It covers the login that leads to the menu or to the shell, each entry type, nesting to 8 and BACK, a held BACK that goes up one level only, the hidden entry, each failure, logout two ways, the reader over the menu with a refused pick and a held BACK, a command step owed under the reader and paid when it closes, `RUN TERMINAL` from the shell in close mode, and the default card. One test reads the screenshot: the frame's pixels are lit where the side and the rule run.

`test_shell.py` uses `shell_card()`, the default card without OPER's `TERMINAL.CFG`, so every default user gets the shell there. Its test programs and those of `test_screens.py` register as `HACK`, since the menu registers `TERMINAL`.

Deliberate breaks, each caught: no `sh_host_uncover()` in the menu's `uncover()` fails `test_covered_menu`, the command never resumes; no on-top check in `menu pick` fails `test_covered_menu`; letting BACK repeat in `tui_map_key()` fails `test_nesting_back_and_held_back` and the host test `tui_held_key_rule`.

### The maintenance request

`test_maint.py` boots UOS alone, with no boot agent, so nothing deletes `/ROBCO/MAINT.REQ`. UOS cannot be asked about the card once it restarts, so the tests read the card image on the host with mtools after QEMU stops. A failed write uses a card with no `/ROBCO` folder and a card with a folder named `MAINT.REQ`. QEMU takes no read-only card, so the read-only case is tested on the host in `test_vfs_card.c`.

The whole road, a request, then the agent, then the maintenance image, is tested in MFBootAgent with a request file the test puts on the card. No test yet runs the real UOS command in front of the real agent.

### The copy step bound

QEMU refuses a read-only drive as an SD card, and the library's target tests make a read-only volume only with a separate `ROBCO_FS_READONLY` build. So the read-only branch of the file system service is tested on the host only, in `test_vfs_card.c`, with a block device flagged read-only.

`test_vfs.py` copies 1 MiB with `fs copy`, one 16 KiB step per loop pass. The loop runs only the events queued before each pass, so input and the tick get their turn between two steps. The slowest step took 2 ticks on `raspi0`, `raspi1ap` and `raspi2b`, and 14 to 25 ticks on `raspi3b`, over a few runs. The bound is `COPY_STEP_BOUND_TICKS`, 50 ticks: about twice the worst figure for a loaded host. A copy in one step, 64 times the work, breaks it on every board.

### The holotape load time

`test_holo.py -s` prints `holo board= bytes= scan_us= load_us= load_ticks=` for a tape of `HF_TAPE_MAX` bytes, 2098728. One `vfs_read_all()` call reads it. Measured in QEMU on the test host, two runs:

| Board | Scan read | Load read | Ticks |
|---|---|---|---|
| `raspi0` | 0.54 and 0.60 s | 0.53 and 0.55 s | 57 and 59 |
| `raspi1ap` | 0.53 and 0.56 s | 0.54 and 0.57 s | 58 and 61 |
| `raspi2b` | 0.54 and 0.59 s | 0.51 and 0.60 s | 56 and 64 |
| `raspi3b` | 3.03 and 3.10 s | 2.71 and 2.96 s | 276 and 301 |

The reader draws a loading line before the read. The test asserts no bound: it prints the figures for the docs.

### Goldens and screenshots

`menu_oper.txt` holds the test menu as OPER sees it at login. `holo_tapes.txt`, `holo_entry.txt`, `hack_start.txt` and `hack_two_guesses.txt` hold the reader's tape list, its first entry in the pager, and the game's board at start and after two wrong guesses with the fixed seed. `tests/golden/first.txt` holds the first screen as text, on a card with no user database: the system screen with the missing-database notice. It has no sizes, CRCs, times or toolchain text. `ROBCO_UPDATE_GOLDEN=1` rewrites it. `ROBCO_SHOT_DIR=<folder>` keeps the screenshots of `test_first.py`.

## The session's sounds

QEMU has no sound. `make sfx-preview` is how the sounds get heard before a board. It boots the test build on raspi2b, or on the board `BOARD` names, with `[audio] sound = on`, `fan = on` and `trace = on`. It drives one session through the maintenance port:

1. A wrong password for `ADMIN`, then the right one.
2. One typed command, `DIRECTORY`.
3. `OPER`'s menu, down three rows.
4. The password game with five cursor moves.
5. A wrong word, then the right word.

Keys go one at a time, 120 ms apart, so each key sound plays whole. A `wait` on the port leaves about half a second of quiet between the parts. The guest's clock makes the gaps, so a slow host only makes them longer. The game uses the test-only fixed seed. A silent boot before the session plays the same game to learn its password.

The target saves the serial log to `build/sfx_preview/serial.log` and dumps the tables to `build/sfx_preview/pcm/` with the library's `tools/sfx_table.py --dump --bed fan`, which also writes each head and rest, so the preview tool finds them by CRC. The library's `tools/audio_preview.py` then renders `build/sfx_preview.wav`. The target fails when the log is missing, when the tool prints any warning, such as a buffer it cannot find, or when the WAV is silent. The WAV is a build product and nothing commits it.

## Screen redraws

`test_screens.py` checks that a screen which closes leaves the display as it was. Before each `dump` it sends `sess state` and `holo state` in one write with `batch()` and waits for `pending=0` in both. The lockout's countdown row changes each second, so that test skips that row.

| Over | Opens and closes | Display after, before the fix | After the fix | Test |
|---|---|---|---|---|
| Login prompt (read line on login) | Holotape reader | Wrong: the reader with a live prompt on one row | Right | `test_reader_over_the_login_prompt` |
| Login prompt | Pager, `page short` | Wrong: the pager's text with a live prompt | Right | `test_pager_over_the_login_prompt` |
| Login prompt | On-screen keyboard | Right: the read screen drew the screen below itself | Right, through `uos_screen_expose()` | `test_keyboard_over_the_login_prompt` |
| Lockout | Holotape reader | Right | Right | `test_reader_over_the_lockout` |
| Holotape reader | Game refusal | Right | Right | `test_refusal_and_pager_over_the_reader` |
| Holotape reader | Pager with an entry | Right | Right | `test_refusal_and_pager_over_the_reader` |
| Shell prompt | Holotape reader | Right | Right | `test_reader_over_a_session` |
| Shell prompt | On-screen keyboard, then the `TYPE /PAGE` pager | Right | Right | `test_pager_and_keyboard_over_the_shell` |
| Terminal menu | Holotape reader, closed with a held BACK | New screen | Right | `test_covered_menu` in `test_menu.py` |

The host test `test_screen.c` covers the order of `draw()` calls. Deliberate breaks, each caught: no expose on pop fails three QEMU tests; an invalidate in place of the expose when the keyboard closes fails the keyboard test; ignoring the `full` flag fails the host order test.

## Hardware checklist

No row below has run on a board yet. Every marker comes from a QEMU test. QEMU does not cover the Pi firmware, `config.txt`, the timing of a real card, real buttons or sound, so these rows are the first check of those parts.

### What you need

- One supported board, see RobCo-Core's [supported boards](../external/robco-core/docs/spec/BOARD_SUPPORT.md#supported-boards), with an HDMI screen.
- A microSD card formatted FAT32 on one partition.
- A 3.3 V USB serial adapter on the PL011: GPIO14 TXD and GPIO15 RXD, at 115200 baud, 8 data bits, no parity, 1 stop bit, no flow control. See [UART policy](../external/robco-core/docs/spec/BOARD_SUPPORT.md#uart-policy). The `@RC` lines below come out there. The maintenance port of the QEMU tests has no pin on a board.
- For the button rows, six push buttons, each between its pin and ground. The pins are in the [settings](ADMIN_GUIDE.md#settings) of the administrator guide.
- For the device row, an LED with a resistor on the pin of `LIGHT1` in `card/UOS/DEVICES.CFG`. The rules for device pins are in [Devices](ADMIN_GUIDE.md#devices).
- The images of the other two stages: `RETROS.IMG` from RETROS-BIOS, `MFBOOT.SYS`, `MAINT.SYS` and `DIAG.SYS` from MFBootAgent, and RETROS-BIOS's `boot/BOOT.CFG`, which has the `MAINT` and `DIAG` entries.

Make the card's files, then copy them to the root of the card:

```sh
make bootdir DIR=path/to/dir RETROS_IMG=path/to/RETROS.IMG MFBOOT_SYS=path/to/MFBOOT.SYS \
    MAINT_SYS=path/to/MAINT.SYS DIAG_SYS=path/to/DIAG.SYS BOOT_CFG=path/to/BOOT.CFG
```

See [A real card](BUILD.md#a-real-card). The users and their passwords are in [Default accounts](USER_MANUAL.md#default-accounts).

### Rows

| Row | Do | See | Proof | Result |
|---|---|---|---|---|
| First stage | Power on | RETROS-BIOS's self test on the screen, then the power-on beep | `@RC stage=bios ev=post_done result=ok`, `ev=settings` with `beep=1`, `ev=load path=/ROBCO/MFBOOT.SYS`, `ev=jump` | |
| Boot agent | Wait | The agent's menu with the entries of `BOOT.CFG` and a countdown, then UOS starts | `@RC stage=bootagent ev=entries`, `ev=countdown state=start`, `ev=choose name=UOS via=timeout`, `ev=jump` | |
| UOS login | Wait for the login screen | `USERNAME:` | `@RC stage=uos ev=handoff trusted=1 prev=2`, `ev=ready screen=login`, `ev=sessdb result=ok users=3` | |
| Login | Log in as `GUEST` on the serial line | The shell banner and the prompt | `@RC stage=uos ev=session state=start user=GUEST how=login`, `ev=session screen=shell` | |
| Shell | Type `SHOW SYSTEM`, `DIRECTORY` and `TYPE NOTICE.TXT`, then `LOGOUT` | Each command's output | `@RC stage=uos ev=sh state=prompt kind=cmd` after each, then `ev=session state=end` | |
| Game by key | Leave `USERNAME:` empty and press F2 | The password game board | `@RC stage=uos ev=login state=hack`, `ev=hack op=start` | |
| Game by button | Leave `USERNAME:` empty and press BACK, keyboard closed | The password game board | `@RC stage=uos ev=login state=hack`, `ev=hack op=start` | |
| Buttons | At `USERNAME:`, press DOWN, then RIGHT, then SELECT | DOWN opens the on-screen keyboard, RIGHT moves its highlight, SELECT types `B` after `USERNAME:` | `@RC stage=uos ev=buttons result=active`, then `ev=osk state=open` | |
| Device by the shell | As `GUEST`, type `DEVICE LIGHT1 ON` in the shell | The LED lights | `@RC stage=uos ev=dev name=LIGHT1 state=ON` | |
| Device by the menu | Log in as `OPER`, which lands in the terminal menu, and pick `Yard lights on or off` | The LED goes dark | `@RC stage=uos ev=session screen=menu`, `ev=tui act=device result=ok`, `ev=dev name=LIGHT1 state=OFF` | |
| Maintenance | As `ADMIN`, type `SET HALT RESTART /MAINTENANCE`. On the agent's maintenance screen, move to the fourth action, "Start the maintenance image", and select it | `MAINTENANCE REQUESTED FOR THE NEXT START.`, the agent's maintenance screen with no countdown, then `TERMINAL MAINTENANCE` | `@RC stage=uos ev=maint_req op=write result=written`, `@RC stage=bootagent ev=maint_open why=request`, `ev=choose name=MAINT`, `@RC stage=maint ev=ready` | |
| Diagnostics | Press a key in the agent's countdown, move to `DIAG` and press Enter. In the image, press `A` to run all | The diagnostics image starts and waits. After `A` it runs its checks | `@RC stage=diag ev=ready`, then `ev=diag_done` after run all | |
| Sound | Set `[audio] sound = on` in `UOS.CFG`, restart, type a wrong then a right password | A low sound, then a short rising one | `@RC stage=uos ev=sound result=on` | |

The diagnostics markers come from MFBootAgent's tests. The others come from `test_chain_terminal.py`, `test_chain_maint.py`, `test_buttons.py`, `test_hack.py`, `test_dev.py` and `test_sound.py`. QEMU has no sound, so the sound row is the first time anyone listens to it on a board.

### Results

| Date | Board | Commit | Rows run | Result |
|---|---|---|---|---|
