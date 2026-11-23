# On-card formats

UOS reads its files from the `/UOS` folder of the card. This page lists every format and gives its exact layout, or links to the module page that holds it. `card/` in the repository is the default card.

| File | Format | Reader | Layout |
|---|---|---|---|
| `/UOS/UOS.CFG` | INI | `src/kernel/settings.c` | [below](#uoscfg) |
| `/UOS/SYSUAF.DAT` | binary, little-endian | `src/session/uaf.c` | [session](modules/session.md#sysuafdat-layout) |
| `/UOS/DEVICES.CFG` | INI | `src/dev/devcfg.c` | [dev](modules/dev.md#file-format), [expander pins](modules/dev.md#expander-pins) |
| `/UOS/HOME/<user>/TERMINAL.CFG` and other `.CFG` menus | INI | `src/tui/tuicfg.c` | [tui](modules/tui.md#terminalcfg) |
| `/UOS/WORDS/Wnn.TXT` | text, one word per line | `src/hack/hack.c` | [hack](modules/hack.md#word-list-format) |
| `/UOS/HOLO/*.HTP` | sealed rimg with an HTXT archive | `src/holo/holofmt.c` | [holo](modules/holo.md#file-layout) |

The INI files use the RobCo-Core INI reader. Comments start with `;` or `#`. LF and CR LF line ends both work. The reader reports keys only, so a section with no keys is never seen. See [Pending library features](ARCHITECTURE.md#pending-library-features).

## UOS.CFG

System settings. Every key is optional. Section and key names, and the word values, are case-insensitive.

```
[system]
name = ROBCO TERMINAL
scheme = green
mode = shell

[login]
retries = 3
lockout_s = 30

[hack]
enabled = yes
user = GUEST
attempts = 4
difficulty = advanced
lockout_s = 60
```

| Section | Key | Value | Default |
|---|---|---|---|
| `system` | `name` | 1 to 31 printable ASCII characters | `ROBCO TERMINAL` |
| `system` | `scheme` | `green`, `amber`, `blue` or `white`. Applies only when the boot info gave no scheme. | unset |
| `system` | `mode` | `shell` or `terminal` | `shell` |
| `login` | `retries` | 1 to 9 | 3 |
| `login` | `lockout_s` | 0 to 3600 | 30 |
| `hack` | `enabled` | `yes`, `on`, `true`, `1` or `no`, `off`, `false`, `0` | yes |
| `hack` | `user` | 1 to 15 of `A-Z 0-9 $ _`, stored upper-case | `GUEST` |
| `hack` | `attempts` | 1 to 9 | 4 |
| `hack` | `difficulty` | `novice`, `advanced`, `expert` or `master` | `advanced` |
| `hack` | `lockout_s` | 0 to 3600 | 60 |

Rules:

- UOS reads at most 4096 bytes.
- A value of the wrong kind is skipped and counted as bad. The default stays.
- A number outside its range is clamped to the nearest limit and counted.
- An unknown key or section is counted.
- When a key repeats, the last one wins.
- A line that does not parse stops the reader. The keys before it apply.

The kernel reads `UOS.CFG` at boot. No module reads the `[login]` or `[hack]` values yet: the login and the minigame are not wired.

## SYSUAF.DAT

The user database: a 32-byte header, then one 96-byte record per user, at most 32. A CRC-32 covers the records. Each password is a salted, iterated SHA-256. `tools/mkuaf.py` builds the file from an INI list, `tools/default_users.ini` for the default card. The privilege bits are the ones in `src/kernel/uos_privs.h`: `SYSTEM` bit 0, `OPER` bit 1, `SECURITY` bit 2, `READALL` bit 3. Layout: [session](modules/session.md#sysuafdat-layout).

## DEVICES.CFG

One `[device NAME]` section per device, up to 24, and one `[expander N]` section per I2C expander chip, up to 4. `gpio = <n>` names a Pi pin. `gpio = X<chip>.<pin>` names an expander pin. Layout and rules: [dev](modules/dev.md#file-format).

## TERMINAL.CFG

A `[terminal]` section with the title and an optional header file, then `[entry N]` sections, at most 16 kept per file. Each entry has a label, a type and a target. Sub-menus are other `.CFG` files in the same home. Layout: [tui](modules/tui.md#terminalcfg).

## Word lists

`/UOS/WORDS/W04.TXT` to `W12.TXT`: one list per word length, 4 to 12. One word per line, `A` to `Z` only, all the same length, at most 512 words. `tools/mkwords.py check` checks the lists. Layout: [hack](modules/hack.md#word-list-format).

## Holotapes

`/UOS/HOLO/*.HTP`: a sealed rimg of subtype 2, data, with an HTXT archive of up to 32 text files as its payload. `tools/mkholo.py` builds a tape from a folder of text files. The sources of the sample tapes are in `card/UOS/HOLO/SRC`. Layout: [holo](modules/holo.md#file-layout).
