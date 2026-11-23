# UOS architecture

This describes the kernel base and the pure modules. Nothing is validated on real hardware.

## Layout

```
src/kernel/   entry, panic, event loop, timers, screens, settings, maintenance port, first screen,
              the shared privilege bits, the service table
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
| `src/kernel/maintreg.c` | The maintenance command table and its word splitting, `uos_maint.h` |
| `src/kernel/service.c` | The rules that run the service table, `uos_service.h` |

The hardware side is `main.c`, `loop.c`, `panic.c`, `maint.c`, `first.c`, `services.c` and `svc_uptime.c`. Their calls are in `uos.h`, `uos_maint.h` and `uos_service.h`.

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
| `mmu` | `mmu_init()` and `mmu_enable()`: the MMU, the caches and branch prediction. `result=on`, `off` or `err`, with `rc`. See [Memory](#memory). |
| `screen` | `fb_init()` at 640x480x32 and `console_init()` on a cleared screen. The scheme comes from a trusted boot info, else green: `source=bootinfo` or `default`. |
| `tick` | IRQs and the tick at 100 Hz. A failure panics. |
| `heap` | The heap from `robco_heap_high()`, from `0x01000000` to the top of ARM memory |
| `input` | The console backend and the maintenance port commands. GPIO is off for now. |
| `selfcheck` | `rimg_self_check()`, run first at entry and reported here. Information only. |
| `mount` | The card by `fs_mount_boot()` with markers `/UOS/UOS.CFG` and `/ROBCO/UOS.SYS`, then the `/UOS` folder: `result=ok`, `none` or `notree` |
| `config` | `UOS.CFG`: `result=ok`, `syntax`, `missing`, `skipped` or `bad`, with every setting and the counts |
| `rtc` | I2C and a DS1307 or DS3231 clock, if one answers |
| `service` | One line per service, in table order: `name`, `rc`. See [Services](#services). |
| `ready` | The first screen is drawn |

When there is no card or no `/UOS` folder, UOS still reaches the first screen. The screen says what is missing, `state` reports it and the maintenance port works.

A service whose init fails does not stop the boot. It sets `error=service`.

A `UOS.CFG` scheme applies only when the boot info gave none. Bad values are skipped, out-of-range numbers are clamped to the range, and both set `error=config`.

## The event loop

One thread of control. `uos_loop_run()` repeats:

1. Takes every input event. Key releases are dropped.
2. Posts one `TICK` event for the ticks since the last one, and one `TIMER` event per timer due.
3. Delivers every queued event, drawing the top screen after each one when it is dirty.
4. Sleeps in `wfi`, through `input_wait()`, until input or the next tick.

Each `TICK` event first calls the `tick` function of every started service, then goes to the screens.

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

`uos_panic(fmt, ...)` and the exception hook for undefined instructions and aborts share one path. It masks IRQs, prints `ev=panic kind=<call|undef|pabt|dabt> reason=<words_joined_by_underscores>`, with the registers after an exception, and shuts the services down with `why=panic`. It clears the screen and shows the reason and the registers. Then it prints `ev=idle` and polls the maintenance port for good.

The MMU stays on in the panic path. The vectors, the stacks and the framebuffer stay mapped, so the screen and the port keep working.

## Reboot and halt

`uos_reboot(source)` shuts the services down with `why=reboot`, prints `ev=reboot source=<source>`, turns the MMU and the data cache off with `mmu_disable()`, which cleans every dirty line to memory, and resets the board through the watchdog.

`uos_halt()` masks IRQs, shuts the services down with `why=halt`, sets the phase to `halt`, prints `ev=halt`, then `ev=idle`, and polls the maintenance port for good, as the panic path does.

## The maintenance port

The mini UART script channel. In QEMU it is the second serial port. On a Pi the mini UART reaches no pin. Each answer is complete lines that start with `| `, then `ok` or `err <reason>`.

The library answers its built-in commands: `key`, `char`, `text`, `dial`, `wait` and `stats`. Every other line goes to the registry of `uos_maint.h`. It looks the first word up, splits the rest into words again and calls the command function. A word in double quotes keeps its spaces, and `""` inside quotes is one quote. A line holds at most 127 characters and 8 blank-separated words in all, quotes or not: the library refuses longer lines. An unknown first word gets `err unknown command`, an unclosed quote `err bad quotes`.

| Command | Answer |
|---|---|
| `help` | One line per registered command, sorted by name: the name, then its help text |
| `state` | `phase=... screen=... user=... card=... trusted=... error=...`: the phase `init`, `ready` or `panic`; the top screen's name; the user; the card `ok`, `none` or `notree`; the trust result; the last error. `none` stands for nothing. |
| `dump` | The console cells, from `console_dump()` |
| `key <name>` | Built in: presses and releases a key |
| `char <c>`, `text <s>` | Built in: characters |
| `type <words>` | The words as characters with single spaces, then SELECT |
| `timers` | `timers count=<n> expired=<n>`, then `timer id=<id> due=<ticks> period=<ticks>` per live timer |
| `reboot` | Shuts the services down, `ev=reboot source=maint`, then a watchdog reset |
| `services` | `service name=<n> rc=<rc>` per service, in table order |
| `uptime` | `uptime s=<seconds> ticks=<ticks> start=<tick>`: the time since the tick started, and the tick at which the uptime service started |
| `fault undef`, `fault pabt`, `panic`, `halt` | Test builds only. They post the fault, `uos_panic()` or `uos_halt()` to the loop. |
| `bench uaf <n>` | Test builds only. Posts `uaf_hash()` over `n` iterations, 1 to 100000, on a fixed salt and password to the loop, which prints `ev=bench name=uaf iterations=<n> rc=<rc> ticks=<ticks> us=<us>` |

The test commands are not registered in the shipped image, and they answer `err unknown command` once the loop has stopped.

## Services

A service starts once at boot and stops before a reboot, a halt or after a panic. `src/kernel/uos_service.h` defines it:

```c
typedef struct uos_service {
    const char *name;
    int  (*init)(void);
    void (*tick)(uint32_t now);
    void (*shutdown)(int why);
} uos_service_t;
```

- `init` runs once, in table order, after the card, the settings and the clock are read, before the loop runs. It returns 0 or a negative code. A failure does not stop the boot: the next service still starts, `state` shows `error=service`, and the failed service never gets `tick` or `shutdown`.
- `tick`, optional, runs from the loop on every `TICK` event, with the tick count.
- `shutdown`, optional, runs in reverse table order before a reboot, a halt and from the panic path. `why` is `UOS_SHUTDOWN_REBOOT`, `UOS_SHUTDOWN_HALT` or `UOS_SHUTDOWN_PANIC`. It runs at most once per boot and must be safe with interrupts off.

Markers: `ev=service name=<n> rc=<rc>` after each init, `ev=shutdown name=<n> why=<reboot|halt|panic>` before each shutdown.

The first service is `uptime`, in `src/kernel/svc_uptime.c`. Its init records the start tick and registers the `uptime` command. Test builds put a service named `testfail` first, whose init returns -5, so the tests see the failure policy.

### How to add a service

1. Write the service in its own file, for example `src/mail/svc.c`, ending with `const uos_service_t uos_svc_mail = { "mail", mail_init, NULL, mail_shutdown };`.
2. In `src/kernel/services.c`, add one line `extern const uos_service_t uos_svc_mail;` under the comment "One extern per service".
3. In the same file, add one line `&uos_svc_mail,` in the `services[]` array between `/* BEGIN SERVICES */` and `/* END SERVICES */`. Put it after every service it uses: the order is the start order.
4. Add the name to `SERVICES` in `tests/uos.py`, so the boot order tests expect its marker.

### How to add a maintenance command

Call `uos_maint_register(name, fn, help)` from the service's `init`. A name is 1 to 12 lower-case letters. The registry holds 48 commands and refuses a duplicate or a library built-in name with `UOS_MAINT_ERR_DUP`.

```c
static int cmd_mail(int argc, char **argv)
{
    if (argc != 0) {
        return uos_maint_fail("unknown command");
    }
    uos_maint_reply("mail count=%u", count);
    return 0;
}
```

`argv[0]` is the first word after the command name. `uos_maint_reply()` sends one complete line per call. `uos_maint_fail(reason)` sets the `err` text and returns a negative code.

A command function runs inside the input layer's script handler. That is in the loop between two events, or after a panic or a halt in the idle poll with IRQs off. It must not block, wait for ticks or do anything that can fault. It posts such work to the loop with `uos_post()`, after it checks that `uos.phase` is `UOS_PHASE_READY`, and the posted call reports its result with a marker. `uos_maint.h` states the same rules.

### console

The console service, `src/console/`, gives every later screen a way to read a line, show a long text and print. Its header is `src/console/con.h`, prefix `con_`. Every `con_` function runs in the loop only. Pure files: `lineedit.c` (the line editor, prefix `le_`) and `conview.c` (layout and text decisions, prefix `cv_`). Thin files: `readline.c`, `pager.c`, `out.c` and `console.c`, the service.

`con_read_line(prompt, flags, max, cb, ctx)` pushes a screen that reads one line of up to 255 characters. The prompt and the line go on the cursor's row, or the next row when the cursor is not in column 0. A prompt gets a blank after it when it lacks one. Only one read runs at a time; a second gets `CON_ERR_BUSY`. When the read ends, the screen pops, then calls `cb` with `CON_OK` or `CON_CANCEL` and the text. The text stays valid until `cb` returns.

Keys of the line editor:

| Key | Does |
|---|---|
| Characters | Insert, or overwrite after Ctrl-O |
| SELECT, Enter | Accept |
| BACK, Backspace, DEL, a lone ESC | Delete before the cursor |
| LEFT, RIGHT, Ctrl-B, Ctrl-F | Move |
| F1, F2, Ctrl-A, Ctrl-E | Start and end of the line |
| F3, Ctrl-D | Delete under the cursor |
| Ctrl-K | Delete to the end |
| UP, DOWN, Ctrl-P, Ctrl-N | Older and newer history lines |
| Ctrl-C | Cancel |
| F4 | Open the on-screen keyboard |

The library's serial parser turns ESC, BS and DEL all into BACK, so BACK is backspace and Ctrl-C cancels. Held keys repeat for moves and deletes, never for SELECT. The history keeps the last 16 lines. It skips empty lines and a repeat of the newest line. An edit of a recalled line does not change the stored one. `CON_RL_MASK` shows `*` per character and never stores or recalls a line. `CON_RL_NOHIST` neither stores nor recalls. A line wider than its window scrolls, with `<` and `>` at the cut ends.

The on-screen keyboard opens on F4 or on any GPIO button event while a line is read. The event that opens it does nothing else. It covers the bottom 5 rows; an input row under it moves to the row above. Each event goes to `osk_handle()` first, and its output goes to the line editor. The console keeps no saved layers, so when the keyboard closes, the read screen calls the `draw()` of the screen below, then draws its own row again. GPIO input is off for now, so the tests open the keyboard with F4 and `osk open`.

`con_page_text(title, text, len, cb, ctx)` pushes a full-screen pager: a title row, 28 rows of body at 78 columns and a status row `line A to B of N` with the keys. It copies the text into the kernel heap with every byte outside printable ASCII as `.`, and allocates the pager's index there. It frees both when the person leaves. Texts up to 1 MiB are accepted; a 64 KiB text wraps in one loop pass. An empty text shows `(no text)`. One pager runs at a time.

`out.c` holds `con_print()`, `con_message()` for `%FAC-S-IDENT, text` lines from `dcl_format_message()`, `con_clear_rows()`, a status row at the bottom (`con_status_set()`, `con_status_clear()`, `con_status_redraw()`) and `con_pause()`, a "press a key" screen. `con_print()` cuts a line at the screen width and also sends it to the PL011. The line being edited is not mirrored on the PL011.

Maintenance commands, for tests and operators:

| Command | Answer |
|---|---|
| `readline <flags> <max> [prompt]` | `ok`, then `ev=line state=reading` and, when the read ends, `ev=line result=<ok\|cancel> len=<n> text=<"text"\|masked>`. Flags are `-`, or `m` for masked and `h` for no history. The text shows a blank, a quote and `%` as `%XX`, and stops at 160 characters with `cut=1`. `err busy` while a read runs. |
| `page <short\|long\|empty\|big>` | A built-in test text. `ev=page top=<n> shown=<n> lines=<n>` on show and on each move, `ev=page result=done` when left. `err busy` while a pager runs. |
| `osk <open\|close\|state>` | `open` and `close` act on the active read, else `err no line`. `state` answers `osk open= layer= row= col= line= reading=`. The keyboard marks `ev=osk state=open` and `ev=osk state=closed`. |

## Memory

| Range | Use |
|---|---|
| `0x00400000` to `0x00FFFFFF` | The OS slot: the image, its data and its stack |
| `0x01000000` up | The heap, from `robco_heap_high()` |

The MMU is on from the `mmu` step. `mmu_init()` builds the library's identity map in 1 MiB sections: all ARM memory from the mailbox as cached RAM, which holds the OS slot, the heap, the boot info area and the vectors; the peripherals as device memory; the core-local peripherals on BCM2836 and BCM2837; everything else unmapped. `fb_init()` runs after it and maps the framebuffer as normal non-cacheable memory, so every console write reaches the screen without a flush. `mbox_call()` handles the cache for its buffers. A null pointer read does not fault: the first megabyte is RAM.

The step runs before the screen and before IRQs are enabled, as RobCo-Core's `memory.md` asks. The reboot path turns the MMU off first, see [Reboot and halt](#reboot-and-halt).

QEMU does not model caches, so the MMU change cannot show a speed-up there. A real Pi should run the login hash and screen copies several times faster with the caches on. That is not measured: nothing is validated on real hardware.

The pure files allocate nothing. The kernel base does not allocate yet; the heap is ready for the services that need it.

## Pending library features

These wait on RobCo-Core changes and a pin bump.

- `ini_parse_sections()`. `ini_parse()` reports keys only. A section with no keys is never seen, and a repeated section header is not reported. `src/dev/devcfg.c` and `src/tui/tuicfg.c` will switch to `ini_parse_sections()` so an empty or repeated section is a counted error. Both files carry a note marked `PENDING LIBRARY FEATURE`. Nothing works around it now.
- Data tapes in `rimg`. `rimg_validate()` cannot accept a data tape, subtype 2, because it looks up a slot first and subtype 2 has none. `tools/rimg.py seal` and `pack` refuse subtype 2 for the same reason. The holo module works around both: `holofmt.c` checks the slot rules itself, and `tools/mkholo.py` writes the header itself. See [holo](modules/holo.md).
- The I2C expander driver. `DEVICES.CFG` already names expander pins, see [dev](modules/dev.md#expander-pins). No glue applies expander operations until the library has the driver.
