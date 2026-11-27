# UOS administrator guide

This guide is for the person who prepares and looks after a terminal's SD card. The person at the terminal reads [USER_MANUAL.md](USER_MANUAL.md). Every command is in [COMMANDS.md](COMMANDS.md), every file layout in [FORMATS.md](FORMATS.md).

Nothing in UOS is validated on real hardware. What follows is how it behaves in QEMU and in the host tests.

## What is on the card

`make sdimage` copies the `card/` folder of the repository onto a FAT32 image and adds `/ROBCO/UOS.SYS`. UOS reads these files:

| Path | What it holds | Read |
|---|---|---|
| `/ROBCO/UOS.SYS` | The UOS image | By the boot stage before UOS |
| `/ROBCO/MAINT.REQ` | A maintenance request, see [Maintenance](#maintenance) | Written by UOS, read by MFBootAgent |
| `/UOS/UOS.CFG` | System settings, see [Settings](#settings) | At boot |
| `/UOS/SYSUAF.DAT` | The users, see [Users](#users) | At boot, and after each change from the terminal |
| `/UOS/SYSUAF.DEF` | A copy of the default users, for the maintenance image | Never by UOS |
| `/UOS/DEVICES.CFG` | Doors, lights and sensors, see [Devices](#devices) | At boot |
| `/UOS/HOME/<home>/` | One folder per user | When the user works |
| `/UOS/HOME/<home>/TERMINAL.CFG` | A user's menu, see [Menus](#menus) | At each login of that user |
| `/UOS/WORDS/W04.TXT` to `W12.TXT` | Word lists of the password game | Each time a game starts |
| `/UOS/HOLO/*.HTP` | Holotapes | Each time the reader opens |
| `/UOS/SYS/HELP/*.HLP` | The text of `HELP` | Each time `HELP` runs |

The card holds only what a terminal reads. The sources of the sample holotapes live in `tools/holotapes/`, and the default users in `tools/default_users.ini`.

The formats are in [FORMATS.md](FORMATS.md). A file the boot reads takes effect at the next start. The others take effect the next time they are read.

You can edit the card on a computer. You can also edit it at the terminal as a user with `SYSTEM`: that user may read and write every file under `/UOS` except `SYSUAF.DAT` and `SYSUAF.DEF`. Other users may write only in their own home. The access table is in [vfs-path](modules/vfs-path.md#access-rules).

## Building a card

| Command | Result |
|---|---|
| `make` | `build/UOS.SYS` |
| `make sdimage` | `build/sd.img`, 64 MiB, FAT32, with `/ROBCO/UOS.SYS` and the `card/` tree |
| `make sdimage CHAIN=1 RETROS_IMG=... MFBOOT_SYS=...` | The same card with RETROS-BIOS, `/ROBCO/MFBOOT.SYS` and a `/ROBCO/BOOT.CFG` that boots UOS |
| `make run BOARD=raspi2b` | Boots `build/sd.img` in QEMU |

The chain card holds no `/ROBCO/MAINT.SYS`, the maintenance image, and its `BOOT.CFG` has no `[maint]` section. Add both by hand if the card needs them. Details: [BUILD.md](BUILD.md).

## Users

### The user file

`/UOS/SYSUAF.DAT` holds up to 32 users. Each user has a name, a password, a home folder, privileges and flags. Passwords are stored as a salted SHA-256, iterated 2000 times on the default card. The layout is in [session](modules/session.md#sysuafdat-layout).

| Field | Rule |
|---|---|
| Name | 1 to 16 of `A-Z 0-9 $ _` |
| Password | 1 to 32 printable ASCII characters. Case matters. |
| Home | 1 to 8 of `A-Z 0-9 $ _ -`, a folder under `/UOS/HOME` |

| Privilege | Gives |
|---|---|
| `SYSTEM` | Everything: every file, every device, every menu entry |
| `OPER` | `SET HALT`, and the devices whose mask asks for it |
| `SECURITY` | `AUTHORIZE`, and reading `SYSUAF.DAT` |
| `READALL` | Reading and listing every home |

| Flag | Effect |
|---|---|
| `DISABLED` | The user cannot log in |
| `NOHACK` | The password game never logs in as this user |

The default card has three users. Change their passwords before the terminal guards anything.

| User | Password | Privileges | Flags |
|---|---|---|---|
| `ADMIN` | `ADMIN-CHANGE-ME` | `SYSTEM` | `NOHACK` |
| `OPER` | `OPER-CHANGE-ME` | `OPER` | `NOHACK` |
| `GUEST` | `GUEST` | none | none |

### Building the user file on a computer

`tools/mkuaf.py` builds the file from an INI list. One section per user:

```ini
[ADMIN]
password = ADMIN-CHANGE-ME
privs = SYSTEM
flags = NOHACK

[BOB]
password = PUMP-42
privs = OPER
home = BOB
```

`privs` and `flags` are comma lists. `home` defaults to the user name. Then:

```
$ python3 tools/mkuaf.py build users.ini -o SYSUAF.DAT
$ python3 tools/mkuaf.py check SYSUAF.DAT
SYSUAF.DAT: 2 users, 2000 iterations
ADMIN            home=ADMIN    privs=SYSTEM flags=NOHACK
BOB              home=BOB      privs=OPER flags=-
```

`build` takes `-o OUTPUT`, `--seed TEXT` for reproducible salts and `--iterations N`, 1 to 100000. Without `--seed` each salt is random. A bad entry stops the build with exit code 1 and a line such as:

```
mkuaf: users.ini: [BOB]: unknown privilege 'ROOT'
```

Two cases print a note on stderr and still build, with exit code 0. A name or home in lower case is stored in upper case, as UOS folds typed names the same way:

```
mkuaf: users.ini: [bob]: note: stored as BOB
```

A file where no enabled user holds `SYSTEM` or `SECURITY` builds too, but nobody can change users at the terminal:

```
mkuaf: users.ini: note: no enabled user holds SYSTEM or SECURITY, nobody can run AUTHORIZE
```

Copy the file to `/UOS/SYSUAF.DAT` and create each home folder under `/UOS/HOME`. The tool makes no folder. The default card is built from `tools/default_users.ini`, with the command in its first lines. In this repository `card/UOS/SYSUAF.DEF` and `card/UOS/SYSUAF.DAT` hold the same bytes, the default users, and `tests/test_maint.py` checks it. On a card of your own, `SYSUAF.DEF` is the recovery set: build it once from accounts whose passwords you store safely, and do not copy the live file over it after each change. See [Lost passwords](#lost-passwords).

### Changing users at the terminal

A user with `SECURITY` or `SYSTEM` uses `AUTHORIZE`. Each change writes the whole file at once and applies at the user's next login. `tests/test_shell.py` runs these commands.

```
$ AUTHORIZE ADD BOB /PRIVS=(OPER,READALL) /NOHACKABLE
Password: *************
Verification: *************
%UAF-S-ADDED, user BOB added
$ AUTHORIZE MODIFY BOB /DISABLED
%UAF-S-MODIFIED, user BOB modified
$ AUTHORIZE REMOVE BOB
%UAF-I-HOMEKEPT, SD0:[HOME.BOB] is not deleted
%UAF-S-REMOVED, user BOB removed
```

- `ADD` also makes `SD0:[HOME.<home>]` with the rights of the user who types it. A user with `SECURITY` alone may not create folders in `HOME`, so the folder is not made. The command prints why, and the user stays added. Make the folder as a `SYSTEM` user.
- A name longer than 8 characters needs `/HOME=`, since a home is at most 8 characters: `AUTHORIZE ADD LONGNAMEUSER` gives `%UAF-E-IVHOME, invalid home folder name`.
- When the home folder is there already, `ADD` uses it as it is and says `%UAF-I-HOMEUSED, SD0:[HOME.<home>] exists already and becomes the home as it is`. A file of that name is not a home: `ADD` reports `%UOS-E-EXISTS` and adds the user all the same. Two users may share a home.
- `MODIFY /HOME=` with a new value never moves the old folder. It prints `%UAF-I-NOMOVE, SD0:[HOME.<old>] is not moved: copy its files by hand`, then makes the new folder as `ADD` does. The same value prints nothing new.
- `REMOVE` never deletes the home folder and says `%UAF-I-HOMEKEPT, SD0:[HOME.<home>] is not deleted`. Delete it by hand when nobody else uses it. You cannot remove yourself: `%UAF-E-SELF, the user who is logged in cannot be removed`.
- UOS keeps at least one enabled user with `SYSTEM` or `SECURITY`. A `REMOVE`, `/PRIVS`, `/NOPRIVS` or `/DISABLED` that would leave none is refused with `%UAF-E-LASTADMIN, <NAME> is the last user who can run AUTHORIZE`, and the file is not written. Give a second user `SYSTEM` or `SECURITY` first. A file that has no such user already stays as it is.
- A user at the command prompt changes their own password with `SET PASSWORD`.
- A user who logs into a menu has no prompt. On the shipped card that is `OPER`, whose menu has no password entry. Change such a password as a user with `SECURITY` or `SYSTEM`: `AUTHORIZE MODIFY OPER /PASSWORD` asks for the new one twice.

### Login rules

These come from `[login]` in `UOS.CFG`. `tests/host/test_session_lockout.c` has a test per rule.

- A wrong password, an unknown user and a disabled account all show the same `ACCESS DENIED.` with the attempts left, and each counts one failure.
- After `retries` failures in a row, 3 by default, the terminal locks for `lockout_s` seconds, 30 by default. With `lockout_s = 0` nothing locks.
- A good login, or a won password game, clears the count.
- A lost password game locks for `[hack] lockout_s`, 60 seconds by default.
- The count and the lock live in memory. A restart clears both.
- `idle_s` ends a session after that many seconds with no key. 0, the default, never ends it.

### Lost passwords

When nobody can log in, the maintenance image can copy `/UOS/SYSUAF.DEF` over `/UOS/SYSUAF.DAT`. That brings back the users and passwords of `SYSUAF.DEF`, whatever they are. On the shipped card they are the default users with their printed passwords. On a card you built, they are what you put there. So keep `SYSUAF.DEF` as a set of accounts whose passwords you know and store safely. If it only mirrors a `SYSUAF.DAT` whose passwords are lost, the copy gives nothing back. It does this only when `/ROBCO/BOOT.CFG` holds `[maint]` with `restore_users = on`. The shipped setting is off. See [Maintenance](#maintenance). Without the maintenance image, copy a good `SYSUAF.DAT` onto the card from a computer.

## Devices

`/UOS/DEVICES.CFG` lists the outputs and sensors on the Pi's GPIO pins and on I2C expander chips. The full rules are in [dev](modules/dev.md#file-format).

```ini
[device LIGHT1]
type = light
gpio = 25
active = low
initial = off
label = Hall lights
privs = 0
```

| Key | Values | Default |
|---|---|---|
| section | `[device NAME]`, NAME 1 to 8 of `A-Z 0-9 _` | required |
| `type` | `door`, `light`, `turret`, `lock`, `relay`, `sensor` | required |
| `gpio` | A Pi pin number, or `X<chip>.<pin>` for an expander pin | required |
| `active` | `high` or `low`: the pin level that means on | `high` |
| `initial` | `on` or `off` at boot | `off` |
| `pulse_ms` | 0, or 10 to 10000: on for that long, then off | 0 |
| `label` | Up to 31 characters | empty |
| `privs` | The privilege bits needed to change it: 1 `SYSTEM`, 2 `OPER`, 4 `SECURITY`, 8 `READALL`. 0 lets anyone. | 2 |

Limits and rules:

- At most 24 devices. UOS reads at most 8 KiB of the file.
- Free Pi pins: 4, 12, 13, 16, 19, 20, 21, 23, 24 and 25. Any other pin is refused with `PINUSED`. The six button pins 5, 6, 17, 22, 26 and 27 stay refused even with `[input] buttons = off`, so turning the buttons on later cannot clash with a device.
- GPIO 0 to 8 are pulled up at reset. An `active = high` output on GPIO 4 loads with the warning `PULLUP`, since it may switch on before UOS starts.
- A device with a bad value is skipped. The others load.
- A key UOS does not know refuses its whole section with `BADKEY`. So `actve = low` for `active = low` skips the device, and its pin is never driven. Key names are lower-case: `Active` is refused too.
- A key given twice in one section refuses that section with `DUPKEY`.
- Both rules apply to `[expander N]` sections. The devices of a refused chip are skipped with `NOCHIP`.
- Keys outside a `[device NAME]` or `[expander N]` section are counted and ignored.
- A user with `SYSTEM` can change every device. Others need every bit of its `privs`.

Each device type takes its own words. `SHOW DEVICES` lists them all, `DEVICE <name> <word>` acts on one:

| Type | Words | States |
|---|---|---|
| door | `ON`, `OFF`, `TOGGLE`, `PULSE`, `OPEN`, `CLOSE` | `OPEN`, `CLOSED` |
| light | `ON`, `OFF`, `TOGGLE`, `PULSE` | `ON`, `OFF` |
| turret | `ON`, `OFF`, `TOGGLE`, `PULSE`, `ACTIVATE`, `STANDBY` | `ACTIVE`, `STANDBY` |
| lock | `ON`, `OFF`, `TOGGLE`, `PULSE`, `UNLOCK`, `LOCK` | `UNLOCKED`, `LOCKED` |
| relay | `ON`, `OFF`, `TOGGLE`, `PULSE` | `ON`, `OFF` |
| sensor | none: it is read only | `TRIPPED`, `CLEAR` |

```
$ DEVICE LIGHT1 ON
LIGHT1   LIGHT  GPIO25  ON     Hall lights
```

`PULSE` needs a `pulse_ms`. A sensor refuses every word with `%DEVICE-E-SENSOR, the device is a sensor and takes no command`.

### Expander chips

UOS drives up to 4 PCA9554 or PCA9554A chips on the I2C bus at GPIO 2 and 3. Declare each chip, then name its pins `X<chip>.<pin>`, pin 0 to 7:

```ini
[expander 0]
addr = 0x20

[device LAMP]
type = light
gpio = X0.3

[device ARM]
type = relay
gpio = 13
```

- `addr` is 0x20 to 0x27 for a PCA9554, 0x38 to 0x3F for a PCA9554A.
- An expander output works only when the file also has a relay named `ARM` on a Pi pin that is off at reset. UOS switches `ARM` on last, after every other output has its initial level. Without `ARM`, every expander output is refused and shows ` REFUSED=NOARM` in `SHOW DEVICES`. Expander sensors still work.
- A chip that does not answer at boot stays unused until the next start. Its outputs show ` UNREACHABLE`, `ARM` stays off and shows ` UNREACH=<count>`, and a command to one of its devices gives `%DEVICE-E-DEVERR, the device command failed`. `tests/test_dev.py` checks a missing chip and a chip at the wrong address.
- A Pi pin has no way to say that nothing is wired to it. UOS writes it and shows the state it wrote.

### When the file is wrong

UOS reports each problem on the serial line, one line per device or chip, such as `deverr name=D2 line=8 why=PINSHARED` or `deverr name=X0 line=15 why=BADKEY`. A key error carries the key's line. `tests/test_dev.py` checks a file with five bad devices: the two good ones load.

When the file has errors, or cannot be read, two places say so:

- The login screen shows `DEVICES: SOME DEVICES REFUSED` on its last header row.
- `SHOW DEVICES` ends with a `%DEVICE-W-CFGERR` line:

| Case | Line |
|---|---|
| The file cannot be read | `%DEVICE-W-CFGERR, DEVICES.CFG cannot be read` |
| One error | `%DEVICE-W-CFGERR, DEVICES.CFG has 1 error: the serial line names it` |
| 2 to 8 errors | `%DEVICE-W-CFGERR, DEVICES.CFG has N errors: the serial line names each` |
| More than 8 errors | `%DEVICE-W-CFGERR, DEVICES.CFG has N errors: the serial line names the first 8` |

The serial line names at most 8 errors. A file that cannot be read gets no `deverr` line, only `devcfg result=bad`. A file over 8192 bytes cannot be read. When no device loads, `SHOW DEVICES` prints the line before `%UOS-E-NODEV`.

A missing `DEVICES.CFG` loads no device and is not an error.

## Holotapes

A holotape is a `.HTP` file in `/UOS/HOLO`, which users see as `SYS$HOLO`. Every user may read them. Only a `SYSTEM` user may add one from the terminal.

- The reader lists the first 32 tapes, by file name, with their titles.
- A tape name is 1 to 8 of `A-Z 0-9 $ _ -`, then `.HTP`.
- A tape holds 1 to 32 entries. An entry is at most 65536 bytes of text, its name at most 31 characters. A tape title is at most 63 characters.
- A tape up to 2 MiB loads. UOS reads it whole into memory each time it opens.

Build a tape from a folder of `.TXT` files, in one of two forms. Without an `ORDER.TXT` file, the first line of each file is the entry's name, the rest is its text, and files go in name order. With an `ORDER.TXT` file, each of its lines reads `FILE.TXT Entry name` and gives the order and the name, and the whole `.TXT` file is the entry's text: do not repeat the name on its first line. `ORDER.TXT` must list every `.TXT` file. The tool needs RobCo-Core's `tools/rimg.py`. It uses `--rimg PATH` if you give it. Otherwise it uses `$ROBCO_CORE/tools/rimg.py` if `ROBCO_CORE` is set. An empty `ROBCO_CORE` counts as not set. Otherwise it uses `external/robco-core/tools/rimg.py` in the UOS checkout it runs from. It never swaps a path you gave for another file: a `--rimg` path or a `ROBCO_CORE` folder without `rimg.py` stops the tool with exit code 3. It also stops with exit code 3 when it finds no `rimg.py` at all. The sources of the sample tapes are in `tools/holotapes/`:

```
$ python3 tools/mkholo.py build --title "The rake with the longer handle" --out STORY.HTP tools/holotapes/STORY
STORY.HTP: 1660 bytes
$ python3 tools/mkholo.py list STORY.HTP
title: The rake with the longer handle
01  The new technician  (332 bytes)
02  The sound in the night  (328 bytes)
03  The order sheet  (264 bytes)
04  Winter  (280 bytes)
$ python3 tools/mkholo.py check card/UOS/HOLO/*.HTP
card/UOS/HOLO/PROCS.HTP: ok
card/UOS/HOLO/STORY.HTP: ok
card/UOS/HOLO/TECHLOG.HTP: ok
```

`tests/tools/test_mkholo.py` checks that the tool and the reader in UOS agree. The format is in [holo](modules/holo.md#file-layout).

## Word lists

The password game reads `/UOS/WORDS/W04.TXT` to `W12.TXT`, one list per word length. Each list holds one word per line, `A` to `Z` only, all of its length, with no repeats. UOS keeps at most 512 words per list.

The difficulty in `UOS.CFG` picks the length and the number of words on the board:

| `difficulty` | Word length | Words |
|---|---|---|
| `novice` | 4 or 5 | 8 |
| `advanced` | 6 to 8 | 12 |
| `expert` | 9 or 10 | 14 |
| `master` | 11 or 12 | 16 |

`tools/mkwords.py check` checks the lists. It fails with exit code 1 only on what UOS refuses: a character outside `A` to `Z`, a wrong length, a repeated word, more than 512 words, fewer words than the board needs, or a file larger than 7232 bytes. UOS counts that size in bytes on the card, line ends included. It prints the rest as `note:` lines and keeps exit code 0: a missing list, an unsorted list, fewer than 64 words, or a word on its deny list. `check --strict` makes every note a failure. The last line counts the good lists present.

```
$ python3 tools/mkwords.py check card/UOS/WORDS
mkwords: 9 lists ok
$ python3 tools/mkwords.py check --strict card/UOS/WORDS
mkwords: 9 lists ok
$ python3 tools/mkwords.py sort card/UOS/WORDS
$ python3 tools/mkwords.py sample 4 --length 5
```

`sort` rewrites each list sorted, without repeats, with LF line ends. A list with a bad line, or with fewer words than the board needs, shows `WORD LIST /UOS/WORDS/Wnn.TXT IS DAMAGED.` when a game starts.

## Menus

A user whose home holds `TERMINAL.CFG` gets a menu after the login instead of the command line. On the default card that is OPER. The menu is the user's whole session: the command line is reached only through a `command` entry the menu offers.

### Writing a menu

A menu is an INI file in the user's home folder, `/UOS/HOME/<home>/TERMINAL.CFG`. Other menus it opens are `.CFG` files in the same folder. The full rules are in [tui](modules/tui.md#terminalcfg).

```ini
[terminal]
title = Pump house
header = WELCOME.TXT

[entry 1]
label = Read the shift notes
type = text
target = NOTES.TXT

[entry 2]
label = Pump controls
type = menu
target = PUMPS.CFG

[entry 3]
label = List my files
type = command
target = DIRECTORY

[entry 4]
label = Pump room light
type = device
target = LIGHT1 TOGGLE

[entry 5]
label = Training tapes
type = holotape

[entry 6]
label = Log out
type = logout
```

- `title` shows on the second row, up to 60 characters. Without one, the screen shows the file name.
- `header` names a text file whose first 8 lines show above the entries.
- `[entry N]` takes N from 1 to 999. Entries show in the order of N, up to 16 a file. Gaps are fine.
- `label` is required, up to 60 characters. `type` is required and lower case.
- A `text`, `menu` or `header` file must be a plain 8.3 name in the same home. A `menu` file ends in `.CFG`.
- A `command` is one command line, as you would type it at `$`, up to 127 characters.
- A `device` target is a device name from `DEVICES.CFG` and an action word.
- A `holotape` target is a tape name in `SYS$HOLO`, or nothing for the list.
- A `logout` entry takes no target.
- `privs = 2` shows the entry only to users who hold OPER. The value is a mask of privilege bits: 1 SYSTEM, 2 OPER, 4 SECURITY, 8 READALL. SYSTEM sees every entry.
- Menus nest 8 deep at most. Each file the menu reads is at most 64 KiB.

Every entry runs with the user's own rights. A menu entry cannot do what the user could not do at the command line.

### Testing a menu

A user at the command line can open their own menu with `RUN TERMINAL`. BACK at its top then closes it. Try a new menu this way before the user logs in to it.

UOS reads and parses `TERMINAL.CFG` at each login before it chooses the screen. It refuses the file when it cannot be read, when it does not parse, or when it parses with no entry this user can see. A refused file never gives the command line to a user without `SYSTEM` or `SECURITY`. That user gets the menu screen with `Bad menu file TERMINAL.CFG (line N)`, `Cannot read TERMINAL.CFG: <reason>`, `No menu entry for this user in TERMINAL.CFG`, the first entry error or `No menu entry in TERMINAL.CFG`, and can only log out. A user with `SYSTEM` or `SECURITY` gets the command line, with one bright line under the shell banner:

```
%UOS-W-BADMENU, TERMINAL.CFG does not parse at line N: command line opened
```

The end of the line is `does not parse at line N`, `cannot be read` or `has no usable entry`. Fix or delete the file from there.

A bad entry inside a good file is skipped and the rest of the menu works. To give a user the command line again, delete their `TERMINAL.CFG`. Users may write in their own home, so a user at the command line can change or delete their own menu.

## Settings

`/UOS/UOS.CFG` is read once at boot, at most 4096 bytes. Every key is optional. Names and word values are case-insensitive. The parser is `src/kernel/settings.c`. Ranges and rules are in [FORMATS.md](FORMATS.md#uoscfg).

| Section | Key | Default | Does |
|---|---|---|---|
| `system` | `name` | `ROBCO TERMINAL` | The terminal's name on the login and menu screens |
| `system` | `scheme` | unset | `green`, `amber`, `blue` or `white`, when the boot stage passed no colour |
| `login` | `retries` | 3 | Failed logins before the lock, 1 to 9 |
| `login` | `lockout_s` | 30 | Seconds of lock, 0 to 3600 |
| `login` | `idle_s` | 0 | Seconds with no key before a session ends, 0 for never |
| `hack` | `enabled` | yes | The password game on or off |
| `hack` | `user` | `GUEST` | The account a won game logs in |
| `hack` | `attempts` | 4 | Wrong guesses allowed, 1 to 9 |
| `hack` | `difficulty` | `advanced` | See [Word lists](#word-lists) |
| `hack` | `lockout_s` | 60 | Seconds of lock after a lost game, 0 to 3600 |
| `watchdog` | `enabled` | off | The hardware watchdog. Keep it off in QEMU. |
| `watchdog` | `timeout_ms` | 8000 | 1000 to 15000 |
| `input` | `buttons` | on | The six panel buttons on GPIO 5, 6, 17, 22, 26 and 27 |
| `audio` | `sound` | off | Turns the sound on. Each key the line editor takes clicks. |
| `audio` | `fan` | off | A quiet fan loop under the other sounds. Needs `sound = on`. Under volume 50 the loop turns to grit, so keep the volume at 50 or more with the fan on. |
| `audio` | `volume` | 80 | 0 to 100 |
| `audio` | `trace` | off | Prints one `ev=audio` line on the serial line for each sound. Leave it off when you work over serial: every key would print a line. |

A bad value keeps the default. A number out of range is brought to the nearest limit. A line that does not parse stops the reader, and the keys before it apply. In each of these cases, and for an unknown key, the first screen and the login screen show `SETTINGS: SOME VALUES IGNORED`. The serial line's `config` marker counts each kind. An old card that still has `mode = shell` counts one unknown key: delete the line. A missing file uses every default.

The buttons are active low: wire each one between its pin and ground. UOS turns on the pull-ups. At boot it waits up to 30 ticks for the stuck-line check. When every line reads pressed for the whole check, as on a terminal with no button panel, UOS turns the buttons off until the next boot and the serial console still works. `SHOW SYSTEM` gives the result on its `Buttons` line.

## Maintenance

An operator, any user with `OPER` or `SYSTEM`, can stop or restart the terminal:

| Command | Does |
|---|---|
| `SET HALT` | Stops. Power must be cycled. |
| `SET HALT RESTART` | Restarts |
| `SET HALT RESTART /MAINTENANCE` | Writes `/ROBCO/MAINT.REQ`, then restarts |
| `SET HALT /MAINTENANCE` | Writes the request, then stops |
| `/NOMAINTENANCE` on either | Removes a request left earlier, then stops or restarts |

```
$ SET HALT RESTART /MAINTENANCE
MAINTENANCE REQUESTED FOR THE NEXT START.
SYSTEM RESTARTING.
```

The request file holds one line, `UOS <user>`. When it cannot be written, the terminal stays up and says why: `%UOS-E-NOMAINT, maintenance request not written: <reason>; the terminal stays up`. `tests/test_maint.py` checks each case.

At the next start MFBootAgent finds the file, deletes it, and opens its maintenance screen with no countdown. From there a person can start the maintenance image, `/ROBCO/MAINT.SYS`, which can check the card, view the config files, set the clock and, when `BOOT.CFG` allows it, restore the default users. UOS alone never deletes the file: on a card with no MFBootAgent the request stays until someone removes it. The rules are in RobCo-Core's boot protocol: [Maintenance request](../external/robco-core/docs/spec/BOOT_PROTOCOL.md#maintenance-request) and [Maintenance image](../external/robco-core/docs/spec/BOOT_PROTOCOL.md#maintenance-image).

## Problems

What an administrator can meet, with the text exactly as UOS shows it.

At boot and at the login:

| Message | Meaning | What to do |
|---|---|---|
| `CARD: NO CARD. INSERT A CARD AND RESTART.` | No card | Insert the card, restart |
| `CARD: NO /UOS FOLDER. SYSTEM FILES ARE MISSING.` | A card without `/UOS` | Copy the `card/` tree onto it |
| `%UOS-W-BADMENU, TERMINAL.CFG <text>: command line opened` | A user with `SYSTEM` or `SECURITY` has a menu file UOS refused | Fix or delete the file, see [Testing a menu](#testing-a-menu) |
| `SETTINGS: SOME VALUES IGNORED` | `UOS.CFG` has a bad, unknown or out-of-range value, or a bad line | Fix the file. The serial line's `config` marker counts each kind. The login screen keeps the line in its header. |
| `USER DATABASE MISSING. NO ONE CAN LOG IN.` | No `/UOS/SYSUAF.DAT` | Copy the file back, press F4 |
| `USER DATABASE DAMAGED (<WHY>). NO ONE CAN LOG IN.` | The file does not load. `<WHY>` names the check, such as `CRC` | Copy a good file back, press F4 |
| `LOGIN IS NOT AVAILABLE WITHOUT A UOS CARD.` | No card | Insert the card, press F4 |
| `LOGIN IS NOT AVAILABLE: NO /UOS FOLDER.` | A card without `/UOS` | As above |
| `TERMINAL LOCKED: TOO MANY FAILED LOGINS.` | Too many failed logins | Wait, or restart |
| `TERMINAL LOCKED: PASSWORD RECOVERY FAILED.` | A lost password game | Wait, or restart |

The password game:

| Message | Meaning |
|---|---|
| `THE PASSWORD GAME IS TURNED OFF ON THIS TERMINAL.` | `[hack] enabled = no` |
| `THE PASSWORD GAME HAS NO TARGET ACCOUNT.` | `[hack] user` names no user |
| `THE TARGET ACCOUNT IS DISABLED.` | That user has `DISABLED` |
| `THE TARGET ACCOUNT CANNOT BE HACKED.` | That user has `NOHACK` |
| `WORD LIST /UOS/WORDS/Wnn.TXT IS MISSING.` | The list for the chosen length is not on the card |
| `WORD LIST /UOS/WORDS/Wnn.TXT IS DAMAGED.` | The list has a bad line, or too few words |
| `THE [hack] SETTINGS ARE REFUSED.` | The game refused the `[hack]` values |

At the command line:

| Message | Meaning |
|---|---|
| `%UOS-E-NOPRIV, not enough privilege for this operation` | The user lacks the privilege, for a file, a device or a command |
| `%UOS-E-PROTECT, file is protected` | A change to `SYSUAF.DAT` or `SYSUAF.DEF` other than through `AUTHORIZE` or `SET PASSWORD` |
| `%UOS-E-NODEV, no such device` | No device of that name, or the card was taken out |
| `%DEVICE-E-DEVERR, the device command failed` | The pin write failed, the expander did not answer, or `ARM` is missing |
| `%DEVICE-E-IVACT, action not valid for this device` | A word the device type does not take |
| `%UAF-E-UAFERR, the user database cannot be changed` | The write of `SYSUAF.DAT` failed, or no user file is loaded |
| `%UAF-E-FULL, the user database is full` | 32 users already |
| `%UAF-E-LASTADMIN, <NAME> is the last user who can run AUTHORIZE` | The change would leave no enabled user with `SYSTEM` or `SECURITY`. Nothing is written |
| `%UOS-E-NOMAINT, maintenance request not written: ...` | No `/ROBCO` folder, a folder named `MAINT.REQ`, a read-only card or a card error |
| `%UOS-E-MREQKEPT, maintenance request not removed: ...` | `/NOMAINTENANCE` could not delete the old request |

In the menu and the holotape reader:

| Message | Meaning |
|---|---|
| `File NAME not found` | A menu entry or header names a file the home does not hold |
| `Bad menu file NAME (line N)` | The menu file does not parse. For `TERMINAL.CFG` at login the user can only log out. |
| `No menu entry for this user in TERMINAL.CFG` | The root menu file parses, but every valid entry needs a privilege the user lacks. The user can only log out. |
| `TERMINAL.CFG: E errors, first: <error> (line L)` with no entry | The root menu file parses, but every entry is bad. Fix the entry at line L. The user can only log out. |
| `No menu entry in TERMINAL.CFG` | The root menu file parses, but holds no entry. The user can only log out. |
| `No holotape NAME.HTP` | A holotape entry names a tape that is not in `SYS$HOLO` |
| `This card has no holotape folder, SYS$HOLO.` | No `/UOS/HOLO` |
| `DAMAGED:` and a reason, in place of a title | The tape fails its checks. Run `tools/mkholo.py check` on it. |
| `Holotapes in SYS$HOLO: N, first 32 shown` | More than 32 tapes |
| `NOT ENOUGH MEMORY TO LOAD THIS HOLOTAPE.` | The tape does not fit in free memory |
