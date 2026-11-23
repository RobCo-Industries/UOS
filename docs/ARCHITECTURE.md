# UOS architecture

This describes the kernel base. Nothing is validated on real hardware.

## Layout

```
src/kernel/   entry, panic, event loop, timers, screens, settings, maintenance port, first screen
src/vfs/ src/console/ src/dcl/ src/session/ src/cmd/ src/tui/ src/hack/ src/osk/ src/dev/ src/holo/
              later modules, one folder each
card/         the default card content: /UOS/UOS.CFG
tests/host/   host tests of the pure files
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
