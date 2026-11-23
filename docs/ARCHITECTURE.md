# UOS architecture

This describes the kernel base and the pure modules. Nothing is validated on real hardware.

## Layout

```
src/kernel/   entry, panic, event loop, timers, screens, settings, maintenance port, first screen,
              the shared privilege bits
src/dcl/ src/vfs/ src/session/ src/osk/ src/dev/ src/tui/ src/hack/ src/holo/
              the pure modules, one folder each
card/         the default card: /UOS/UOS.CFG, SYSUAF.DAT, DEVICES.CFG, HOME/, WORDS/, HOLO/
tools/        card tools: mkuaf.py, mkwords.py, mkholo.py
tests/host/   host tests of the pure files, and host tools such as holocheck.c
tests/tools/  pytest tests of the card tools
tests/        QEMU target tests
```

## The pure split

A pure file has no hardware access, no kernel calls and no allocation. Its state lives in structs the caller owns. Every decision lives in pure code and has host tests. Hardware-side files are thin.

| Pure file | Role |
|---|---|
| `src/kernel/event.c` | The event queue, `uos_event.h` |
| `src/kernel/timerq.c` | Timers in ticks, `uos_timer.h` |
| `src/kernel/screen.c` | The screen stack, `uos_screen.h` |
| `src/kernel/settings.c` | The `UOS.CFG` reader, `uos_settings.h` |
| `src/kernel/bootlines.c` | The first screen's text, `bootlines.h` |

The hardware side is `main.c`, `loop.c`, `panic.c`, `maint.c` and `first.c`. Their services are in `uos.h`.

The modules below are pure too. The image links them, but no kernel code calls them yet, so the linker drops them.

## Modules

Each module has a reference page in `docs/modules/`. The on-card formats are gathered in [FORMATS.md](FORMATS.md). No login, shell or screen uses a module yet.

Every module that tests privileges uses the bits of `src/kernel/uos_privs.h`: `SYSTEM` bit 0, `OPER` bit 1, `SECURITY` bit 2, `READALL` bit 3. Each module keeps its own names, such as `DCL_PRIV_OPER` or `UAF_PRIV_OPER`, as aliases, and static asserts check them.

[dcl](modules/dcl.md): the command language in the style of DCL. It splits a line into a verb, qualifiers and parameters, looks names up by the abbreviation rule, checks the privileges each verb needs and formats the system messages. Pure files: `src/dcl/dcl.c`, `msg.c`, `tables.c`. Public header: `src/dcl/dcl.h`, prefix `dcl_`.

[vfs path](modules/vfs-path.md): file specifications such as `SD0:[HOME.ADMIN]A.TXT;1`. It turns them into FAT paths and back, expands logical names and wildcards, and decides file access. Pure file: `src/vfs/path.c`. Public header: `src/vfs/path.h`, prefix `vp_`.

[session](modules/session.md): users and login. It holds SHA-256, the `SYSUAF.DAT` user database with its iterated password hash, and the login lockout. Pure files: `src/session/sha256.c`, `uaf.c`, `lockout.c`. Public headers: `sha256.h`, `uaf.h`, `lockout.h`, prefixes `sha_`, `uaf_`, `lk_`.

[osk](modules/osk.md): the on-screen keyboard for text entry with buttons only. It keeps the highlight and the layer and emits key events for the line editor. Pure file: `src/osk/osk.c`. Public header: `src/osk/osk.h`, prefix `osk_`.

[dev](modules/dev.md): device control. It parses `DEVICES.CFG`, keeps each device's state, turns commands into pin operations with their timing and formats status lines. Pure files: `src/dev/devcfg.c`, `devstate.c`. Public header: `src/dev/dev.h`, prefix `dv_`.

[tui](modules/tui.md): the menu-driven terminal screen and the pager. It parses `TERMINAL.CFG`, runs the menu model and lays out the screen. Pure files: `src/tui/tuicfg.c`, `tui.c`, `pager.c`. Public header: `src/tui/tui.h`, prefixes `tui_` and `pg_`.

[hack](modules/hack.md): the password minigame. It parses the word lists, picks the candidates, builds the board and runs the game with its lockout timer. Pure file: `src/hack/hack.c`. Public header: `src/hack/hack.h`, prefix `hk_`.

[holo](modules/holo.md): data holotapes. It validates a tape in memory, gives its title and entries, and runs the reader model. Pure files: `src/holo/holofmt.c`, `holoread.c`. Public header: `src/holo/holofmt.h`, prefix `hf_`.

## Boot order

Each step prints one marker, `@RC stage=uos ev=<name> ...`, in this order:

| Marker | Step |
|---|---|
| `boot` | The PL011 at 115200. `version`, `entry_mode`. |
| `handoff` | The trust rule of RobCo-Core's `BOOT_PROTOCOL.md`: `trusted`, `prev` stage, `flags`. A cold boot copies the firmware blob. The exception hook goes to the panic path. |
| `maint` | The mini UART for the maintenance port: `mini` is its init result |
| `board` | MIDR, SoC, peripheral base, board revision |
| `mbox` | The ARM memory from the mailbox |
| `screen` | `fb_init()` at 640x480x32 and `console_init()` on a cleared screen. The scheme comes from a trusted boot info, else green: `source=bootinfo` or `default`. |
| `tick` | IRQs and the tick at 100 Hz. A failure panics. |
| `heap` | The heap from `robco_heap_high()`, from `0x01000000` to the top of ARM memory |
| `input` | The console backend and the maintenance port commands. GPIO is off for now. |
| `selfcheck` | `rimg_self_check()`, run first at entry and reported here. Information only. |
| `mount` | The card by `fs_mount_boot()` with markers `/UOS/UOS.CFG` and `/ROBCO/UOS.SYS`, then the `/UOS` folder: `result=ok`, `none` or `notree` |
| `config` | `UOS.CFG`: `result=ok`, `syntax`, `missing`, `skipped` or `bad`, with every setting and the counts |
| `rtc` | I2C and a DS1307 or DS3231 clock, if one answers |
| `ready` | The first screen is drawn |

When there is no card or no `/UOS` folder, UOS still reaches the first screen. The screen says what is missing, `state` reports it and the maintenance port works.

A `UOS.CFG` scheme applies only when the boot info gave none. Bad values are skipped, out-of-range numbers are clamped to the range, and both set `error=config`.

## The event loop

One thread of control. `uos_loop_run()` repeats:

1. Takes every input event. Key releases are dropped.
2. Posts one `TICK` event for the ticks since the last one, and one `TIMER` event per timer due.
3. Delivers every queued event, drawing the top screen after each one when it is dirty.
4. Sleeps in `wfi`, through `input_wait()`, until input or the next tick.

Event types, `uos_event.h`: `KEY`, `CHAR`, `TICK`, `TIMER` and `CALL`. `uos_post(fn, arg)` queues a call from any context. The queue holds 32 events and counts drops.

### Timers

`uos_timer_start(delay, period, fn, arg, &id)` starts a timer in ticks: once when `period` is 0, else every `period` ticks. At expiry the loop runs `fn(arg)`, or, when `fn` is NULL, gives the screens a `TIMER` event. `uos_timer_cancel(id)` ends it. A timer never fires after its cancel returns, even when its expiry is already queued: the loop checks the id first. Deadlines compare by signed difference, so the tick counter may wrap. Delays and periods go up to `0x7fffffff` ticks. At most 16 timers live at once.

A periodic timer fires on the first loop pass at or after its deadline. A late loop catches up one expiry per pass, so the count stays exact.

### Screens

A screen is a struct of `enter`, `leave`, `event` and `draw` callbacks and a `ctx` pointer. The kernel keeps a stack of up to 8:

- `uos_screen_push()` puts a screen on top, such as a pager or the on-screen keyboard. The screen below gets no `leave`.
- `uos_screen_pop()` removes the top. The last screen cannot be popped.
- `uos_screen_replace()` swaps the top.
- Events go to the top first. When its `event()` returns 0, the next screen down gets the event.
- `uos_screen_invalidate()` asks for a redraw. `draw()` paints the whole screen.

## The first screen

`first.c` shows the banner from `bootlines.c`, then after half a second a placeholder: the system name, "login is not built yet", the card state, the settings state, the clock if a clock chip answered, and the last key. A periodic one-second timer refreshes the clock. The session task replaces this screen.

Two keys test the timers. F1 starts a periodic timer of 10 ticks that fires 10 times, with an `ev=timer name=probe` marker each time. F2 starts a one-shot timer that a second timer cancels before it is due, and a third timer reports whether it fired.

The banner lines are lore: in-world text, original to UOS.

## The panic path

`uos_panic(fmt, ...)` and the exception hook for undefined instructions and aborts share one path. It masks IRQs, prints `ev=panic kind=<call|undef|pabt|dabt> reason=<words_joined_by_underscores>`, with the registers after an exception, clears the screen and shows the reason and the registers. Then it prints `ev=idle` and polls the maintenance port for good.

## The maintenance port

The mini UART script channel. In QEMU it is the second serial port. On a Pi the mini UART reaches no pin. Each answer is complete lines that start with `| `, then `ok` or `err <reason>`.

| Command | Answer |
|---|---|
| `state` | `phase=... screen=... user=... card=... trusted=... error=...`: the phase `init`, `ready` or `panic`; the top screen's name; the user; the card `ok`, `none` or `notree`; the trust result; the last error. `none` stands for nothing. |
| `dump` | The console cells, from `console_dump()` |
| `key <name>` | Built in: presses and releases a key |
| `char <c>`, `text <s>` | Built in: characters |
| `type <words>` | The words as characters with single spaces, then SELECT |
| `timers` | `timers count=<n> expired=<n>`, then `timer id=<id> due=<ticks> period=<ticks>` per live timer |
| `reboot` | `ev=reboot source=maint`, then a watchdog reset |
| `fault undef`, `fault pabt`, `panic` | Test builds only. Refused in the shipped image. |

## Memory

| Range | Use |
|---|---|
| `0x00400000` to `0x00FFFFFF` | The OS slot: the image, its data and its stack |
| `0x01000000` up | The heap, from `robco_heap_high()` |

The pure files allocate nothing. The kernel base does not allocate yet; the heap is ready for the services that need it.

## Pending library features

These wait on RobCo-Core changes and a pin bump.

- `ini_parse_sections()`. `ini_parse()` reports keys only. A section with no keys is never seen, and a repeated section header is not reported. `src/dev/devcfg.c` and `src/tui/tuicfg.c` will switch to `ini_parse_sections()` so an empty or repeated section is a counted error. Both files carry a note marked `PENDING LIBRARY FEATURE`. Nothing works around it now.
- Data tapes in `rimg`. `rimg_validate()` cannot accept a data tape, subtype 2, because it looks up a slot first and subtype 2 has none. `tools/rimg.py seal` and `pack` refuse subtype 2 for the same reason. The holo module works around both: `holofmt.c` checks the slot rules itself, and `tools/mkholo.py` writes the header itself. See [holo](modules/holo.md).
- The I2C expander driver. `DEVICES.CFG` already names expander pins, see [dev](modules/dev.md#expander-pins). No glue applies expander operations until the library has the driver.
