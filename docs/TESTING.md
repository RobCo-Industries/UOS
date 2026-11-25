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
| `test_timerq.c` | Timers: one-shot, periodic, catch-up, the tick wrap, cancel before and after expiry, limits. Delivery through a full event queue: a one-shot, a periodic timer over several periods, a cancel while pending, 16 timers due, a timer started from a callback. The event queue. |
| `test_screen.c` | The screen stack rules: push, pop, replace, events passed down, draws |
| `test_settings.c` | `UOS.CFG`: every key, clamps, bad values, unknown keys, syntax errors, the `[watchdog]` keys, the shipped card's watchdog off |
| `test_hwdog.c` | The hardware watchdog's rules: off unless enabled and in range, a kick every quarter of the timeout, across the tick wrap, none from a dead loop, none after a shutdown, halt stops it and panic leaves it |
| `test_bootlines.c` | The text of the first screen |
| `test_maintreg.c` | The maintenance command table: name rules, duplicates and library built-ins refused, sorted order, the 48-command limit; the word splitting with quotes, the word and line limits; joining the channel's words back |
| `test_service.c` | The service rules with fake services: inits in order, a failure does not stop the next, ticks only for started services, shutdown in reverse order and once, nothing before init, the table limit |
| `test_privs.c` | Every module's privilege names equal the shared bits of `uos_privs.h`; the `SYSUAF.DAT` names of each bit |
| `test_dcl.c` | The DCL tokeniser, continuation, numbers, the abbreviation rule, qualifiers and parameters, prompts, privileges |
| `test_dcl_msg.c` | The text of every system message, the file messages `WRITLCK`, `PROTECT`, `TOOBIG` and `INSFMEM` included |
| `test_dcl_table.c` | The UOS command table: every verb and keyword, the table check |
| `test_vfs_path.c` | File specifications: parse, resolve, format, logical names, wildcards, listing order, the access table; a file directly in `HOME` with a stale name past its depth |
| `test_vfs_rules.c` | The vfs service's rules: error words, the message map with its read-only, protected, too-large and memory messages, library and path codes, the date format, work names, wildcard targets, parent and entry paths, the listing filter, the bounded sorted insert, the recovery pick |
| `test_vfs_svc.c` | The `fs` command of `svc.c` over `vfs.c` on a host image, with a fake loop that runs one posted call at a time: a three-file copy keeps its target and privileges when the default and the user change between steps, `fs default` and `fs who` refused while it runs, a posted request keeps the context of its post, `fs free` on a small image and on a sparse 6 GiB FAT32 image |
| `test_u64dec.c` | `uos_u64_dec()`: 0, 1, 2^32 - 1, 2^32, 10^12, 10^19 and 2^64 - 1, 64 values checked against libc, buffers one byte short |
| `test_vfs_card.c` | `vfs.c` over the library's FatFs glue on a host image: every entry point after an atomic write cut between its renames, on a writable card and on a read-only one, and `DEV:` left alone |
| `test_session_sha256.c` | SHA-256: the standard short and long messages, a million `a` streamed, block boundaries, length carry |
| `test_session_uaf.c` | `SYSUAF.DAT`: layout, every validation check, the hash vectors shared with `mkuaf.py`, verify with equal work, edits, privilege names, the default card's file |
| `test_session_lockout.c` | The login lockout: counts, the lock time, the tick wrap |
| `test_osk.c` | The on-screen keyboard: both layers, moves with wrap, every key, emitted events, the row text |
| `test_dev.c` | `DEVICES.CFG`: every key and error, the refused pin masks, the pull-up warning, expander pins, empty and repeated sections with the header's line; commands, pulses, the tick wrap, status lines, the pin record, the default card's file |
| `test_dev_svc.c` | The device service's rules: debounce, `ARM` last at boot and first at shutdown, the reverse safe order, operations mapped to calls through a fake GPIO layer, expander failures, an expander bus error owed and retried, expander writes skipped on panic, the chip in the pin record, `ARM` held at boot for each kind of output failure and armed by hand with the unreachable count, pulse ends from the tick only, late and across the wrap; the device codes mapped to file system codes for `DEV:` |
| `test_tui.c` | The menu model: moves, every action, sub-menus to eight levels, scrolling, headers, privileges, the layout |
| `test_tui_cfg.c` | `TERMINAL.CFG`: every key, target and error; the menus of the default card |
| `test_tui_pager.c` | The pager: line ends, wrapping, moves and keys, a 64 KiB text |
| `test_card_menus.c` | Each device entry in the default card's menus names a device of its `DEVICES.CFG` and a word that device takes |
| `test_hack.c` | Word lists, candidate picks, 1000 boards per difficulty, the one-junk gap, the cursor, brackets, the game and its lockout, the default card's lists |
| `test_holo.c` | Tape validation step by step, every error, limits, the reader model |
| `test_console_lineedit.c` | The line editor: every key, insert in the middle, the 255 and per-call limits, a full line, history from empty to 17 lines, recalled edits, masked lines, the scroll window, repeats, redraw results; the exact serial bytes of each redraw, masked, scrolled and at the largest size |
| `test_console_view.c` | The console's decisions: the input row under the keyboard, when the keyboard opens, line widths, the pager status row, the byte filter, marker quoting, command words |

## Tool tests

`make test-tools` builds the host tools the tool tests call, then runs pytest over `tests/tools/` from the venv. `tests/host/holocheck.c` is such a tool: it validates tapes with the C code, so the Python and C checks can be compared.

| File | Covers |
|---|---|
| `test_mkuaf.py` | `tools/mkuaf.py`: build, check, the shared vectors, every bad input, the default card's file |
| `test_mkwords.py` | `tools/mkwords.py`: check, sort and sample over good and bad lists, the default card's lists |
| `test_mkholo.py` | `tools/mkholo.py`: build, list, check, every bad input, agreement with `holocheck` |

## Target tests

`make test-target BOARD=<board>` runs pytest on raspi0, raspi1ap, raspi2b and raspi3b. The tests build the cards they need with mtools. They drive UOS through the maintenance port and match only complete serial lines.

| File | Covers |
|---|---|
| `test_entry.py` | Every bring-up marker in order; the trust rule after `examples/chain1`, with a good and a broken boot info; the whole chain |
| `test_card.py` | No card, no `/UOS` folder, no `UOS.CFG`, a syntax error, bad and clamped values, the scheme and the name |
| `test_loop.py` | Keys from the port and the serial console reach the screen; a periodic timer fires the right number of times in its bound; a cancelled timer never fires; F2 twice runs one cancel probe; the timer listing; a key flood during the banner still shows the first screen in its bound, and the services tick through it; `type` with 1, 15, 30 and 31 characters |
| `test_panic.py` | The panic path for an undefined instruction, a prefetch abort and a `uos_panic()` call; a failed tick and a fault right after the exception hook, both before the input layer starts, with `state`, `dump` and `reboot` answering; in both, `help` lists exactly the base commands, `timers` answers and `type` answers `err not ready`; `reboot`; an `nm` check that the shipped image has no test hooks |
| `test_first.py` | A golden of the first screen's text and screenshots |
| `test_services.py` | The `ev=service` markers and `services`; `uptime`; shutdown markers in reverse order on `reboot`, halt and panic; a fault after the services started: one shutdown each with `why=panic`, none again on the reboot, and `help` lists every command; a failing service in test builds; `help`, unknown commands and quotes; the `bench uaf` figure |
| `test_vfs.py` | The vfs service through `fs`: every specification form, the read limit, missing files and folders, a folder read as a file; listings with wildcards, an empty folder and more entries than the maximum; write, overwrite, must-not-exist, mkdir, rename, copy of a small and a 1 MiB file with CRCs, wildcard delete, files kept across a reboot with no work file left; cells of the access table; a full card keeps the old file; no card and no `/UOS`; the `DEV:` hook with a fake and with none; cards built with work files only, `#NAME` only, `~NAME` only and both, as copy sources and targets and for stat, new, delete, rename and mkdir; `fs default` and `fs who` refused during a three-file copy, which lands every file in its first target; `fs free` against the size and free bytes read from the card image. A card over 4 GiB is too large for a test image: `test_vfs_svc.c` covers it on the host |
| `test_console.py` | Reading a line with edit keys, history, cancel and the maximum; a masked read that stays masked after a refused second read; the on-screen keyboard typing a word over both layers and leaving the screen below intact; the pager over each test text; goldens of the open keyboard and the long text's first page; command errors and a second read refused; the exact PL011 echo of a typed and edited line and of a masked one |
| `test_dev.py` | The device service: the shipped `DEVICES.CFG`, the boot record with `ARM` last, `dev set` for every action, privileges, pulse timing, two pulses, cancel, a stalled loop, sensor debounce, bad files with an empty section, expander pins on QEMU's `pca9554` model: an output that switches and reads back and a sensor on a chip input, a missing chip that leaves its devices unreachable and `ARM` off, a chip at a wrong address left untouched, the expander output safe on reboot and left as it is on panic; the `ARM` rule: an expander output with no `ARM` refused while a Pi device and an expander sensor work, `ARM` on an expander refused, and a panic with an expander output on, then a reboot that sets it to `initial` before `ARM`; no file, no card, the safe state on reboot and panic, the software watchdog reset, the hardware watchdog off by default, and the hardware watchdog setting that resets the board at start. Pulses must end within their length plus 1 tick plus `HOST_ALLOWANCE_TICKS`, 20. |
| `test_link.py` | The services together. `DEV:` through `fs`: `cat`, `ls`, writes refused and allowed by each device's own mask, a bad word, a sensor. `DEVICES.CFG` through the file system: a folder, a file over 8 KiB, no file. A read over the pager and the pager over a read. A pulse end and a sensor change during a read. A panic with the on-screen keyboard open. The shutdown order on reboot, halt and panic, with every `devsafe` line before the console stops. A reboot during a stepped copy keeps the old file. After a panic and a halt the pin record does not change for 4 s, `STILL_S`, with a pulse running. |
| `test_env_hygiene.py` | No test copies the host environment |

### How the tests start UOS

- Alone: QEMU loads the image at `0x00400000`. A two-word image at `0x8000` jumps there with no boot info.
- After `examples/chain1`: the library's first stage builds a boot info and an amber console, then enters a sealed two-word image at `0x00200000`. That image jumps to UOS and keeps the registers, so UOS gets chain1's boot info.
- The whole chain: with `--retros-img` and `--mfboot-sys`, or `RETROS_IMG=` and `MFBOOT_SYS=` for make, RETROS-BIOS boots from a card that holds MFBootAgent and UOS. Without both, the test is skipped.

### Test builds

`UOS_TEST_HOOKS=1` adds the `fault`, `panic`, `halt` and `bench` maintenance commands, `fs pattern` and `fs fakedev`, and a service named `testfail` whose init returns -5, first in the table. The tests build it in `build/testhooks/`. `UOS_TEST_TICK_FAIL=1` makes `tick_init()` report a failure, in `build/tickfail/`. `UOS_TEST_EARLY_FAULT=1` runs an undefined instruction just after the exception hook, in `build/earlyfault/`. None is shipped: `test_panic.py` checks with `nm` that the shipped image has none of their symbols.

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

### The copy step bound

QEMU refuses a read-only drive as an SD card, and the library's target tests make a read-only volume only with a separate `ROBCO_FS_READONLY` build. So the read-only branch of the file system service is tested on the host only, in `test_vfs_card.c`, with a block device flagged read-only.

`test_vfs.py` copies 1 MiB with `fs copy`, one 16 KiB step per loop pass. The loop runs only the events queued before each pass, so input and the tick get their turn between two steps. The slowest step took 2 ticks on `raspi0`, `raspi1ap` and `raspi2b`, and 14 to 25 ticks on `raspi3b`, over a few runs. The bound is `COPY_STEP_BOUND_TICKS`, 50 ticks: about twice the worst figure for a loaded host. A copy in one step, 64 times the work, breaks it on every board.

### Goldens and screenshots

`tests/golden/first.txt` holds the first screen as text. It has no sizes, CRCs, times or toolchain text. `ROBCO_UPDATE_GOLDEN=1` rewrites it. `ROBCO_SHOT_DIR=<folder>` keeps the screenshots of `test_first.py`.
