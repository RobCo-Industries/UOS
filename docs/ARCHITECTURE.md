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

### vfs

The file system service, `src/vfs/vfs.h`, prefix `vfs_`. Commands and screens reach card files through it with file specifications. Each call resolves the specification with the caller's `vp_ctx_t`, checks access with `vp_access()`, acts through the library's FatFs glue and returns 0 or a negative `VFS_E_*` code. `vfs_msg()` maps a code to the command language's message, so every command prints the same message for the same failure. The command language has no read-only or protected message: both map to `NOPRIV`.

Every call that touches the card runs from the loop only. The service reads `uos.card` on each call: no card gives `VFS_E_NOCARD`, no `/UOS` folder `VFS_E_NOTREE`. Its init cannot fail.

- `vfs_read_all()` reads a whole file into a heap buffer. A file over the caller's limit is `VFS_E_TOOBIG` and nothing is read.
- `vfs_write_all()` uses `fs_write_file_atomic()`. New content goes to `~NAME`, then replaces `NAME`. After a power cut the target holds the whole old or the whole new content, or is absent for a first write. The library finishes or undoes leftover work files on the next read or write of that target. Init does not sweep the card. Listings hide work files. A full card is `VFS_E_FULL` and keeps the old file.
- `vfs_list()` returns the first `max` entries in the path module's order, and the total. It hides work files, names that are not 8.3 and entries the access table hides from the caller. Dates are FAT dates, shown as `DD-MMM-YYYY HH:MM:SS`.
- `vfs_iter_begin()` resolves a wildcard to at most 128 files and checks access per file. DELETE, RENAME and COPY use it.
- `vfs_copy_begin()` and `vfs_copy_step()` copy in steps of 16 KiB. The new file is written as `~NAME` and moved into place through `#NAME`, the library's recovery names. `vfs_copy()` runs every step in one call, for small files.
- `vfs_dev_register()` takes the device service's `read`, `write` and `list` hooks for `DEV:` names. Without hooks, `DEV:` calls return `VFS_E_NODEV`.
- `vfs_sys_read_all()` and `vfs_sys_write_all()` skip the access check, for system files such as `/UOS/SYSUAF.DAT`.

The decisions are pure, in `src/vfs/vfsrules.c`, with host tests.

The `fs` maintenance command acts as a test user. `fs who <home|-> <privs>` and `fs default <spec>` set the user and the default and answer at once. The others post to the loop, answer `ok` and end with a marker `ev=fs op=<op> rc=<rc> err=<word>`:

| Command | Marker fields |
|---|---|
| `fs ls [spec] [max]` | One `entry= size= date= time= dir=` marker per entry, then `count= total= more=` |
| `fs cat <spec> [max]` | `len=` and the first line as `line=`, spaces as `_`. Default type `TXT`, default max 65536 |
| `fs crc <spec>` | `len=` and `crc=`, CRC-32 of up to 4 MiB |
| `fs write <spec> <words>`, `fs new <spec> <words>` | The words and a line end. `new` refuses an existing file |
| `fs del <spec>`, `fs ren <from> <to>` | One `file=` marker per file, then `files=` and `skipped=` |
| `fs copy <from> <to>` | One `file=` marker per file, then `files=`, `skipped=`, `steps=` and `maxticks=` |
| `fs mkdir <spec>`, `fs stat <spec>`, `fs free` | `stat` adds `exists=`, the entry fields; `free` adds `total=` and `free=` |

Test builds add `fs pattern <spec> <bytes>` and `fs fakedev on|off`, a fake device service with `DOOR1` and `LAMP2`.

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
