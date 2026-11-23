# Testing UOS

Every test runs on the host or in QEMU. Nothing is validated on real hardware.

## Host tests

`make test-host` builds each `tests/host/test_*.c` with the host compiler, `-Werror` and the address and undefined behaviour sanitizers. Each test links every pure file. It also compiles the tests and the pure files with the cross GCC, to catch warnings only GCC reports.

| File | Covers |
|---|---|
| `test_timerq.c` | Timers: one-shot, periodic, catch-up, the tick wrap, cancel before and after expiry, limits. Delivery through a full event queue: a one-shot, a periodic timer over several periods, a cancel while pending, 16 timers due, a timer started from a callback. The event queue. |
| `test_screen.c` | The screen stack rules: push, pop, replace, events passed down, draws |
| `test_settings.c` | `UOS.CFG`: every key, clamps, bad values, unknown keys, syntax errors |
| `test_bootlines.c` | The text of the first screen |

## Target tests

`make test-target BOARD=<board>` runs pytest on raspi0, raspi1ap, raspi2b and raspi3b. The tests build the cards they need with mtools. They drive UOS through the maintenance port and match only complete serial lines.

| File | Covers |
|---|---|
| `test_entry.py` | Every bring-up marker in order; the trust rule after `examples/chain1`, with a good and a broken boot info; the whole chain |
| `test_card.py` | No card, no `/UOS` folder, no `UOS.CFG`, a syntax error, bad and clamped values, the scheme and the name |
| `test_loop.py` | Keys from the port and the serial console reach the screen; a periodic timer fires the right number of times in its bound; a cancelled timer never fires; the timer listing; a key flood during the banner still shows the first screen in its bound; `type` with 1, 15, 30 and 31 characters |
| `test_panic.py` | The panic path for an undefined instruction, a prefetch abort and a `uos_panic()` call; `reboot`; an `nm` check that the shipped image has no test hooks |
| `test_first.py` | A golden of the first screen's text and screenshots |
| `test_env_hygiene.py` | No test copies the host environment |

### How the tests start UOS

- Alone: QEMU loads the image at `0x00400000`. A two-word image at `0x8000` jumps there with no boot info.
- After `examples/chain1`: the library's first stage builds a boot info and an amber console, then enters a sealed two-word image at `0x00200000`. That image jumps to UOS and keeps the registers, so UOS gets chain1's boot info.
- The whole chain: with `--retros-img` and `--mfboot-sys`, or `RETROS_IMG=` and `MFBOOT_SYS=` for make, RETROS-BIOS boots from a card that holds MFBootAgent and UOS. Without both, the test is skipped.

### Test builds

`UOS_TEST_HOOKS=1` adds the `fault` and `panic` maintenance commands. The tests build it in `build/testhooks/`. It is never shipped: `test_panic.py` checks with `nm` that the shipped image has none of its symbols.

### Timing bound

The timer probe runs 10 periods of 10 ticks. It must end after at least 100 ticks and at most 100 ticks plus 1 tick of loop latency plus `HOST_ALLOWANCE_TICKS`, 20 ticks. The allowance covers a loaded test host. The count must be exact.

The key flood sends 1500 `text` lines of 32 characters in one write while the banner runs. The `ready` marker reports `banner=<ticks>` since the screen was entered. It must be at least 50 ticks and at most 50 ticks plus 1 tick of loop latency plus `HOST_ALLOWANCE_TICKS`.

### Goldens and screenshots

`tests/golden/first.txt` holds the first screen as text. It has no sizes, CRCs, times or toolchain text. `ROBCO_UPDATE_GOLDEN=1` rewrites it. `ROBCO_SHOT_DIR=<folder>` keeps the screenshots of `test_first.py`.
