# Testing UOS

Every test runs on the host or in QEMU. Nothing is validated on real hardware.

## Host tests

`make test-host` builds each `tests/host/test_*.c` with the host compiler, `-Werror` and the address and undefined behaviour sanitizers. Each test links every pure file. It also compiles the tests and the pure files with the cross GCC, to catch warnings only GCC reports. `make all` compiles every module source with the target flags and `-Werror`.

The tests run from the repository root, so the ones that read the default card open `card/UOS/...` by a relative path. Before they build, `tools/mkuaf.py vectors-h` writes `build/host/gen/uaf_vectors.h` from `tests/vectors/uaf.json`.

| File | Covers |
|---|---|
| `test_timerq.c` | Timers: one-shot, periodic, catch-up, the tick wrap, cancel before and after expiry, limits. Delivery through a full event queue: a one-shot, a periodic timer over several periods, a cancel while pending, 16 timers due, a timer started from a callback. The event queue. |
| `test_screen.c` | The screen stack rules: push, pop, replace, events passed down, draws |
| `test_settings.c` | `UOS.CFG`: every key, clamps, bad values, unknown keys, syntax errors |
| `test_bootlines.c` | The text of the first screen |
| `test_maintreg.c` | The maintenance command table: name rules, duplicates and library built-ins refused, sorted order, the 48-command limit; the word splitting with quotes, the word and line limits; joining the channel's words back |
| `test_service.c` | The service rules with fake services: inits in order, a failure does not stop the next, ticks only for started services, shutdown in reverse order and once, nothing before init, the table limit |
| `test_privs.c` | Every module's privilege names equal the shared bits of `uos_privs.h`; the `SYSUAF.DAT` names of each bit |
| `test_dcl.c` | The DCL tokeniser, continuation, numbers, the abbreviation rule, qualifiers and parameters, prompts, privileges |
| `test_dcl_msg.c` | The text of every system message |
| `test_dcl_table.c` | The UOS command table: every verb and keyword, the table check |
| `test_vfs_path.c` | File specifications: parse, resolve, format, logical names, wildcards, listing order, the access table |
| `test_session_sha256.c` | SHA-256: the standard short and long messages, a million `a` streamed, block boundaries, length carry |
| `test_session_uaf.c` | `SYSUAF.DAT`: layout, every validation check, the hash vectors shared with `mkuaf.py`, verify with equal work, edits, privilege names, the default card's file |
| `test_session_lockout.c` | The login lockout: counts, the lock time, the tick wrap |
| `test_osk.c` | The on-screen keyboard: both layers, moves with wrap, every key, emitted events, the row text |
| `test_dev.c` | `DEVICES.CFG`: every key and error, the refused pin masks, the pull-up warning, expander pins; commands, pulses, the tick wrap, status lines, the pin record, the default card's file |
| `test_tui.c` | The menu model: moves, every action, sub-menus to eight levels, scrolling, headers, privileges, the layout |
| `test_tui_cfg.c` | `TERMINAL.CFG`: every key, target and error; the menus of the default card |
| `test_tui_pager.c` | The pager: line ends, wrapping, moves and keys, a 64 KiB text |
| `test_card_menus.c` | Each device entry in the default card's menus names a device of its `DEVICES.CFG` and a word that device takes |
| `test_hack.c` | Word lists, candidate picks, 1000 boards per difficulty, the one-junk gap, the cursor, brackets, the game and its lockout, the default card's lists |
| `test_holo.c` | Tape validation step by step, every error, limits, the reader model |

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
| `test_loop.py` | Keys from the port and the serial console reach the screen; a periodic timer fires the right number of times in its bound; a cancelled timer never fires; F2 twice runs one cancel probe; the timer listing; a key flood during the banner still shows the first screen in its bound; `type` with 1, 15, 30 and 31 characters |
| `test_panic.py` | The panic path for an undefined instruction, a prefetch abort and a `uos_panic()` call; a failed tick and a fault right after the exception hook, both before the input layer starts, with `state`, `dump` and `reboot` answering; `reboot`; an `nm` check that the shipped image has no test hooks |
| `test_first.py` | A golden of the first screen's text and screenshots |
| `test_services.py` | The `ev=service` markers and `services`; `uptime`; shutdown markers in reverse order on `reboot`, halt and panic; a failing service in test builds; `help`, unknown commands and quotes; the `bench uaf` figure |
| `test_env_hygiene.py` | No test copies the host environment |

### How the tests start UOS

- Alone: QEMU loads the image at `0x00400000`. A two-word image at `0x8000` jumps there with no boot info.
- After `examples/chain1`: the library's first stage builds a boot info and an amber console, then enters a sealed two-word image at `0x00200000`. That image jumps to UOS and keeps the registers, so UOS gets chain1's boot info.
- The whole chain: with `--retros-img` and `--mfboot-sys`, or `RETROS_IMG=` and `MFBOOT_SYS=` for make, RETROS-BIOS boots from a card that holds MFBootAgent and UOS. Without both, the test is skipped.

### Test builds

`UOS_TEST_HOOKS=1` adds the `fault`, `panic`, `halt` and `bench` maintenance commands, and a service named `testfail` whose init returns -5, first in the table. The tests build it in `build/testhooks/`. `UOS_TEST_TICK_FAIL=1` makes `tick_init()` report a failure, in `build/tickfail/`. `UOS_TEST_EARLY_FAULT=1` runs an undefined instruction just after the exception hook, in `build/earlyfault/`. None is shipped: `test_panic.py` checks with `nm` that the shipped image has none of their symbols.

### Timing bound

The timer probe runs 10 periods of 10 ticks. It must end after at least 100 ticks and at most 100 ticks plus 1 tick of loop latency plus `HOST_ALLOWANCE_TICKS`, 20 ticks. The allowance covers a loaded test host. The count must be exact.

The key flood sends 1500 `text` lines of 32 characters in one write while the banner runs. The `ready` marker reports `banner=<ticks>` since the screen was entered. It must be at least 50 ticks and at most 50 ticks plus 1 tick of loop latency plus `HOST_ALLOWANCE_TICKS`.

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

### Goldens and screenshots

`tests/golden/first.txt` holds the first screen as text. It has no sizes, CRCs, times or toolchain text. `ROBCO_UPDATE_GOLDEN=1` rewrites it. `ROBCO_SHOT_DIR=<folder>` keeps the screenshots of `test_first.py`.
