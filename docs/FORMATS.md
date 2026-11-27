# On-card formats

UOS reads its files from the `/UOS` folder of the card. This page lists every format and gives its exact layout, or links to the module page that holds it. `card/` in the repository is the default card.

| File | Format | Reader | Layout |
|---|---|---|---|
| `/UOS/UOS.CFG` | INI | `src/kernel/settings.c` | [below](#uoscfg) |
| `/UOS/SYSUAF.DAT` | binary, little-endian | `src/session/uaf.c` | [session](modules/session.md#sysuafdat-layout) |
| `/UOS/SYSUAF.DEF` | as `SYSUAF.DAT` | the maintenance image, not UOS | [below](#sysuafdef) |
| `/ROBCO/MAINT.REQ` | one optional text line | MFBootAgent, not UOS | [below](#maintreq) |
| `/UOS/DEVICES.CFG` | INI | `src/dev/devcfg.c` | [dev](modules/dev.md#file-format), [expander pins](modules/dev.md#expander-pins) |
| `/UOS/HOME/<user>/TERMINAL.CFG` and other `.CFG` menus | INI | `src/tui/tuicfg.c` | [tui](modules/tui.md#terminalcfg) |
| `/UOS/WORDS/Wnn.TXT` | text, one word per line | `src/hack/hack.c` | [hack](modules/hack.md#word-list-format) |
| `/UOS/HOLO/*.HTP` | sealed rimg with an HTXT archive | `src/holo/holofmt.c` | [holo](modules/holo.md#file-layout) |
| `/UOS/SYS/HELP/*.HLP` | plain text | `src/shell/shcmds.c` | [below](#help-files) |

The INI files use the RobCo-Core INI reader. Comments start with `;` or `#`. LF and CR LF line ends both work. `DEVICES.CFG` and `TERMINAL.CFG` go through `ini_parse_sections()`, which also reports each section header, so a section with no keys or a repeated section is a counted error.

## UOS.CFG

System settings. Every key is optional. Section and key names, and the word values, are case-insensitive.

```
[system]
name = ROBCO TERMINAL
scheme = green

[login]
retries = 3
lockout_s = 30
idle_s = 0

[hack]
enabled = yes
user = GUEST
attempts = 4
difficulty = advanced
lockout_s = 60

[watchdog]
enabled = off
timeout_ms = 8000

[input]
buttons = on

[audio]
sound = off
fan = off
volume = 80
trace = off
```

| Section | Key | Value | Default |
|---|---|---|---|
| `system` | `name` | 1 to 31 printable ASCII characters | `ROBCO TERMINAL` |
| `system` | `scheme` | `green`, `amber`, `blue` or `white`. Applies only when the boot info gave no scheme. | unset |
| `login` | `retries` | 1 to 9 | 3 |
| `login` | `lockout_s` | 0 to 3600 | 30 |
| `login` | `idle_s` | 0 to 86400 seconds with no key before a session ends, 0 for never | 0 |
| `hack` | `enabled` | `yes`, `on`, `true`, `1` or `no`, `off`, `false`, `0` | yes |
| `hack` | `user` | 1 to 15 of `A-Z 0-9 $ _`, stored upper-case | `GUEST` |
| `hack` | `attempts` | 1 to 9 | 4 |
| `hack` | `difficulty` | `novice`, `advanced`, `expert` or `master` | `advanced` |
| `hack` | `lockout_s` | 0 to 3600 | 60 |
| `watchdog` | `enabled` | as `hack` `enabled`. Turns the hardware watchdog on. Keep it off in QEMU, which resets at once when it starts | off |
| `watchdog` | `timeout_ms` | 1000 to 15000: the time without a loop tick before the board resets | 8000 |
| `input` | `buttons` | as `hack` `enabled`. Starts the six panel buttons at boot | on |
| `audio` | `sound` | as `hack` `enabled`. Starts the audio driver and the rest ramp at boot, and plays the key sounds. | off |
| `audio` | `fan` | as `hack` `enabled`. Read and kept. Nothing uses it yet. | off |
| `audio` | `volume` | 0 to 100 | 80 |
| `audio` | `trace` | as `hack` `enabled`. Prints the audio driver's trace lines on the serial line. Off, UOS drops them. | off |

Rules:

- UOS reads at most 4096 bytes.
- A value of the wrong kind is skipped and counted as bad. The default stays.
- A number outside its range is clamped to the nearest limit and counted.
- An unknown key or section is counted.
- When a key repeats, the last one wins.
- A line that does not parse stops the reader. The keys before it apply.

The kernel reads `UOS.CFG` at boot. The session service applies `[login]`, see [session](modules/session.md#the-session-service). The device service reads `[watchdog]`, see [The watchdogs](ARCHITECTURE.md#the-watchdogs). The `buttons` boot step reads `[input]`, see [Boot order](ARCHITECTURE.md#boot-order). The sound service reads `[audio]`, see [Sound](ARCHITECTURE.md#sound). The password game reads `[hack]` each time it starts, see [hack](modules/hack.md#the-screen):

- `enabled = no` refuses the game.
- `user` is the account a won game logs in. The game refuses an account that does not exist, is disabled or has the `NOHACK` flag. The shipped default is `GUEST`, the account with no privilege. `ADMIN` and `OPER` carry `NOHACK` on the default card.
- `attempts` is the number of wrong guesses before the lockout.
- `difficulty` sets the word length and the number of words.
- `lockout_s` is how long a lost game locks the terminal. 0 locks nothing.

## SYSUAF.DAT

The user database: a 32-byte header, then one 96-byte record per user, at most 32. A CRC-32 covers the records. Each password is a salted, iterated SHA-256. The session service reads the file at boot, and again after `SET PASSWORD` or `AUTHORIZE` wrote it whole with an atomic write; a missing or damaged file stops every login, see [session](modules/session.md#the-session-service). `tools/mkuaf.py` builds the file from an INI list, `tools/default_users.ini` for the default card. The privilege bits are the ones in `src/kernel/uos_privs.h`: `SYSTEM` bit 0, `OPER` bit 1, `SECURITY` bit 2, `READALL` bit 3. Layout: [session](modules/session.md#sysuafdat-layout).

## SYSUAF.DEF

The default user database, the same bytes as the card's fresh `SYSUAF.DAT`. `card/UOS/SYSUAF.DEF` is a copy of `card/UOS/SYSUAF.DAT`, so `make sdimage` puts both on the card. The maintenance image's restore users tool copies it over `SYSUAF.DAT` when every password is lost, see RobCo-Core's `BOOT_PROTOCOL.md`, "Maintenance image". UOS never writes it. The path module protects it as it protects `SYSUAF.DAT` for reading, `SYSTEM` or `SECURITY`, and refuses every change for every user with `%UOS-E-PROTECT`, see [vfs-path](modules/vfs-path.md). Rebuild both files together when `tools/default_users.ini` changes.

## MAINT.REQ

The maintenance request, `/ROBCO/MAINT.REQ` on the boot volume. Its presence asks MFBootAgent for its maintenance screen at the next boot; the agent deletes it first. `SET HALT /MAINTENANCE` creates it with a plain create, not an atomic write, and writes one line, `UOS <user>` and CR LF. The format is RobCo-Core's: `BOOT_PROTOCOL.md`, "Maintenance request".

## DEVICES.CFG

One `[device NAME]` section per device, up to 24, and one `[expander N]` section per I2C expander chip, up to 4. `gpio = <n>` names a Pi pin. `gpio = X<chip>.<pin>` names an expander pin. Layout and rules: [dev](modules/dev.md#file-format).

- A repeated `[device NAME]` or `[expander N]` header is refused, even when the first section was empty or bad. The first one wins.
- Keys are case-sensitive. An unknown key, or a key given twice, refuses its whole section with `BADKEY` or `DUPKEY`. Keys outside a section are counted and ignored.
- An expander output needs a relay named `ARM` on a Pi pin that is off at reset, such as `gpio = 25` with `active = high`. Without one, every expander output is refused with `NOARM` and never driven. Expander inputs still work.

## TERMINAL.CFG

A `[terminal]` section with the title and an optional header file, then `[entry N]` sections, at most 16 kept per file. Each entry has a label, a type and a target. Sub-menus are other `.CFG` files in the same home. Layout: [tui](modules/tui.md#terminalcfg).

As built:

- The file sits in the user's home, `/UOS/HOME/<home>/TERMINAL.CFG`. A user whose home holds it gets the menu after the login in place of the shell.
- Every file the menu names, sub-menus, the header and `text` targets, is read from the home with the user's rights, up to 64 KiB.
- `command` targets run as the user, with the user's privileges, as typed at the `$` prompt.
- `device` targets go to the device service with the user's privileges. The device's own mask applies.
- `holotape` targets name a tape in `SYS$HOLO`: 1 to 8 of `A-Z 0-9 $ _ -`, then nothing or `.HTP`. A name with no type means `.HTP`.
- On the default card only OPER has a menu.

## Word lists

`/UOS/WORDS/W04.TXT` to `W12.TXT`: one list per word length, 4 to 12. One word per line, `A` to `Z` only, all the same length, at most 512 words. `tools/mkwords.py check` checks the lists and fails only on what UOS refuses; `--strict` also wants every list present, sorted, at least 64 words and free of its deny list. Layout: [hack](modules/hack.md#word-list-format).

## Holotapes

`/UOS/HOLO/*.HTP`: a sealed rimg of subtype 2, data, with an HTXT archive of up to 32 text files as its payload. `tools/mkholo.py` builds a tape from a folder of text files. The sources of the sample tapes are in `tools/holotapes`, outside the card. Layout: [holo](modules/holo.md#file-layout).

## Help files

`/UOS/SYS/HELP/<name>.HLP`, the folder of the logical name `SYS$HELP:`: one plain text file per verb, which `HELP <verb>` prints as it is. LF or CR LF line ends; lines over 80 columns wrap. At most 64 KiB. The command table in `src/dcl/tables.c` names each file, cut to 8.3: `DIRECTOR.HLP` for `DIRECTORY`, `AUTHORIZ.HLP` for `AUTHORIZE`. `HELP` with no topic prints `HELP.HLP`. `test_shell.c` checks that the default card has a file for every verb.
