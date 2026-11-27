# UOS commands

This is the reference of every command of the UOS shell. The command table in `src/dcl/tables.c` defines the verbs, keywords and qualifiers; [dcl](modules/dcl.md) has the syntax rules. The shell is described in [ARCHITECTURE.md](ARCHITECTURE.md#the-shell). Nothing is validated on real hardware.

## How commands read

- A verb may be shortened to any unique prefix: `DIR` is `DIRECTORY`, `SH DEV` is `SHOW DEVICES`.
- A qualifier starts with `/`. A qualifier marked "neg" also takes the `/NO` form.
- A line that ends with `-` continues on the next line, at the `_$` prompt.
- A missing parameter is asked for at a `_<name>:` prompt. An empty answer asks again. Ctrl-C drops the command.
- File specifications are those of [vfs path](modules/vfs-path.md): `SD0:[HOME.ADMIN]A.TXT`, `[.SUB]`, `SYS$LOGIN:`, `SYS$HELP:`, `BOOT:`, `DEV:`. `*` matches any characters, `%` one.
- Each file of a wildcard is checked against the access table again, so `DELETE *.*` never deletes `SYSUAF.DAT` and never touches a file the user may not delete.
- A wildcard counts the matches the access table refused. Three outcomes follow, for `DIRECTORY`, `TYPE`, `COPY`, `DELETE` and `RENAME` alike:
  - every match refused: `%UOS-E-NOPRIV`;
  - some refused: the command acts on the others, then prints `%UOS-I-SKIPPED, N files skipped: not enough privilege` before its count message, and the `cmd state=end` marker has `skipped=N`;
  - nothing matched: `%UOS-W-NOFILES`, with `skipped=0`.
- `/OUTPUT=file` replaces the file only when the command succeeds, with a final message of severity `S` or `I`. An error, a warning such as `%UOS-W-NOCLOCK` or `%UOS-W-NOFILES`, Ctrl-C or text over 64 KiB leave an existing file exactly as it was. The new text goes in with one atomic write, so a power cut leaves the whole old or the whole new file.
- Ctrl-C ends the running command and returns to the prompt with `%UOS-I-CANCEL, operation cancelled`. It works in steps, at a `/CONFIRM` question, at a password, at the `CREATE` lines, in the `TYPE /PAGE` pager, with the on-screen keyboard open, and in a program. Files a loop finished stay done; nothing half-written is left.
- An empty or comment-only line at the prompt does nothing. Through `sh run` or `sh_run_line()` it is refused.
- Every message comes from the catalogue in `src/dcl/msg.c` through one map, `sh_err_msg()`. The messages are `%FACILITY-S-IDENT, text`, with `S` the severity: `S` success, `I` information, `W` warning, `E` error.

## State of each verb

| Verb | State |
|---|---|
| `DIRECTORY` | Full |
| `TYPE` | Full |
| `COPY` | Full. The target must not exist. |
| `DELETE` | Full. Folders cannot be deleted: the file system service has no folder delete. |
| `RENAME` | Full |
| `CREATE` | Full |
| `SET DEFAULT`, `SET TERMINAL`, `SET PASSWORD` | Full |
| `SET HALT` | Done |
| `SHOW` | Full. `SHOW USERS` lists the one session of this terminal. |
| `HELP` | Full |
| `RUN` | Full: `TERMINAL`, `HOLOTAPE` and `HACK` are installed |
| `LOGIN` | Full |
| `LOGOUT` | Full |
| `ROBCO` | Full |
| `DEVICE` | Full |
| `HOLOTAPE` | Full |
| `AUTHORIZE` | Full |

## DIRECTORY

Format: `DIRECTORY [file-spec]`

Lists a folder, or the files that match a wildcard. With no parameter, the current folder. The listing shows at most 256 entries and says how many more there are.

| Qualifier | Does |
|---|---|
| `/SIZE`, neg | The size in bytes |
| `/DATE`, neg | The FAT date and time |
| `/FULL`, neg | Size and date |
| `/OUTPUT=file` | Writes the listing to a file, default type `.LIS`, only on success |

Privileges: none; the access table decides which folders a user may list.

Messages: `%UOS-W-NOFILES` when nothing matches, `%UOS-E-NOPRIV` when the access table hid every match, `%UOS-E-DNF`, `%UOS-E-IVSPEC`. When it hid some, the listing ends with a `%UOS-I-SKIPPED` line.

```
$ DIR *.TXT /SIZE
Directory *.TXT

DUTY.TXT             692
NOTICE.TXT           194

Total of 2 files, 886 bytes.
```

## TYPE

Format: `TYPE file-spec`

Shows a text file, up to 1 MiB. With a wildcard, shows each file under its full name. Long lines wrap at 80 columns. Output runs 8 lines per step, so Ctrl-C stops it.

| Qualifier | Does |
|---|---|
| `/PAGE`, neg | Shows the text in the pager instead: UP and DOWN a line, LEFT and RIGHT a page, `q` leaves |

Messages: `%UOS-E-FNF`, `%UOS-E-DNF`, `%UOS-E-NOPRIV`, `%UOS-E-TOOBIG`.

```
$ TYPE NOTICE.TXT /PAGE
```

## COPY

Format: `COPY from-spec to-spec`

Copies files in steps of 16 KiB. `to-spec` may be a folder, or use `*` for the name or the type. A target that exists is refused with `%UOS-E-EXISTS`: a copy never replaces a file. Ctrl-C stops the copy; the work file is removed and no target is left.

| Qualifier | Does |
|---|---|
| `/LOG`, neg | `%COPY-I-FILCOPY, <file> copied` per file |
| `/CONFIRM`, neg | `<file>, copy? [N]:` per file. `Y`, `YES`, `T`, `1` copy; `N`, an empty line or `F` skip; `A` copies this file and the rest; `Q` stops and reports the files done; Ctrl-C cancels the command. Anything else asks again. |

Messages: `%COPY-S-COPIED, N files copied`, then any per-file error such as `%UOS-E-EXISTS` with the file named.

```
$ COPY *.TXT [.BACKUP] /LOG
%COPY-I-FILCOPY, SD0:[HOME.ADMIN]DUTY.TXT;1 copied
%COPY-I-FILCOPY, SD0:[HOME.ADMIN]NOTICE.TXT;1 copied
%COPY-S-COPIED, 2 files copied
```

## DELETE

Format: `DELETE file-spec`

Deletes files, one per step. The user database is protected: `DELETE SD0:[000000]SYSUAF.DAT` gives `%UOS-E-PROTECT`, and a wildcard skips it.

| Qualifier | Does |
|---|---|
| `/LOG`, neg | `%DELETE-I-FILDEL, <file> deleted` per file |
| `/CONFIRM`, neg | `<file>, delete? [N]:` per file, answers as for `COPY` |

Messages: `%DELETE-S-DELETED, N files deleted`, `%UOS-I-SKIPPED` before it when some matches were refused, `%UOS-W-NOFILES` when nothing matched, `%UOS-E-NOPRIV` when every match was refused. `DELETE SD0:[000000]SYSUAF.*` gives `%UOS-E-NOPRIV`.

```
$ DELETE OLD*.TXT /CONFIRM
OLD1.TXT;1, delete? [N]: Y
OLD2.TXT;1, delete? [N]: N
%DELETE-S-DELETED, 1 file deleted
```

## RENAME

Format: `RENAME from-spec to-spec`

Renames files or moves them to another folder of the same tree. The target must not exist.

| Qualifier | Does |
|---|---|
| `/LOG`, neg | `%RENAME-I-FILREN, <file> renamed` per file |
| `/CONFIRM`, neg | `<file>, rename? [N]:` per file |

Messages: `%RENAME-S-RENAMED, N files renamed`, `%UOS-E-EXISTS`.

```
$ RENAME DRAFT.TXT [.DONE]REPORT.TXT
%RENAME-S-RENAMED, 1 file renamed
```

## CREATE

Format: `CREATE file-spec`

Makes a new file from typed lines at the `>` prompt. A line that holds only `.` ends the text and writes the file in one atomic write. Ctrl-C drops it. At most 16 KiB. A file that exists is refused.

| Qualifier | Does |
|---|---|
| `/DIRECTORY` | Makes a folder instead: `CREATE [.LOGS] /DIRECTORY` |
| `/LOG`, neg | Prints `%CREATE-S-CREATED, <spec> created` |

Messages: `%CREATE-S-CREATED`, `%UOS-E-EXISTS`, `%UOS-E-TOOBIG`.

```
$ CREATE SHIFT.TXT
> NIGHT SHIFT: CHECK THE PUMPS.
> .
```

## SET

### SET DEFAULT

Format: `SET DEFAULT folder`. Changes the current folder for the session. The folder must exist and the user must be allowed to list it. `[-]` goes up; `SYS$LOGIN:` goes home. Messages: `%UOS-E-DNF`, `%UOS-E-NOPRIV`, `%UOS-E-IVSPEC`.

### SET PASSWORD

Format: `SET PASSWORD`. Asks `Old password:`, `New password:` and `Verification:`, each masked with `*`. The new password is 1 to 32 printable characters. The change is written to `SYSUAF.DAT` with a fresh salt from the hardware RNG, and the session service reads the file again. Messages: `%SET-S-PWDSET, password changed`, `%SET-E-BADPWD`, `%SET-E-PWDNOTVER`, `%UAF-E-IVPWD`, `%UAF-E-UAFERR`.

### SET TERMINAL

Format: `SET TERMINAL /SCHEME=GREEN|AMBER|BLUE|WHITE`. Changes the screen colors until the next boot. Marker `ev=term scheme=<name>`.

### SET HALT

Format: `SET HALT [RESTART] [/MAINTENANCE]`. Needs `OPER`.

- `SET HALT` shuts the services down and halts, as the kernel's `uos_halt()` does: the screen stays, the maintenance port answers, power must be cycled.
- `SET HALT RESTART` shuts the services down and resets the board through `uos_reboot("shell")`.
- `/MAINTENANCE` asks the boot agent for its maintenance screen at the next start. The shell creates `/ROBCO/MAINT.REQ`, the request file of RobCo-Core's `BOOT_PROTOCOL.md`, "Maintenance request". It holds one line, `UOS <user>`. A file already there stays as it is. MFBootAgent deletes the file at the next boot and opens the maintenance screen.
- `SET HALT RESTART /MAINTENANCE` writes the request, then restarts. The spec names only this form.
- `SET HALT /MAINTENANCE` writes the request, then halts: the next power-on opens the maintenance screen. This form is UOS's own addition.
- When the file cannot be written, the command ends with `%UOS-E-NOMAINT` and the reason, and the terminal neither halts nor restarts. Causes: no `/ROBCO` folder, a folder named `MAINT.REQ`, a read-only card, a card error.
- `/NOMAINTENANCE` removes a request file an earlier command left, then halts or restarts. No file is fine. When the delete fails, the command ends with `%UOS-E-MREQKEPT` and nothing halts.
- Without either qualifier, the command leaves the request file alone.
- Marker `ev=maint_req op=<write|clear> result=<written|kept|removed|none|failed> rc=<word>`, before the halt or restart. `rc` is a `vfs_strerror()` word, `ok` on success.
- UOS alone never deletes the file. A card that boots UOS with no agent keeps the request until an agent or a person removes it.

```
$ SET HALT RESTART
SYSTEM RESTARTING.
$ SET HALT RESTART /MAINTENANCE
MAINTENANCE REQUESTED FOR THE NEXT START.
SYSTEM RESTARTING.
```

## SHOW

Format: `SHOW keyword [/OUTPUT=file]`. `/OUTPUT` writes the text to a file, only on success: `SHOW TIME /OUTPUT` with no clock chip ends with a warning and leaves the file alone.

| Keyword | Shows |
|---|---|
| `DEFAULT` | The current folder |
| `USERS` | The session of this terminal: user, terminal, home, privileges |
| `SYSTEM` | Version, system name, uptime, card state, devices, users, and a `Buttons` line with the boot step's result: `active`, `disabled_stuck`, `off`, `error` or `checking` |
| `TIME` | The date from the clock chip. With no chip, the uptime and `%UOS-W-NOCLOCK`. |
| `DEVICES [name]` | The device header and one status line per device, read through `DEV:`. When `DEVICES.CFG` had errors or could not be read, the list ends with a `%DEVICE-W-CFGERR` line, such as `%DEVICE-W-CFGERR, DEVICES.CFG has N errors: the serial line names each`. The [admin guide](ADMIN_GUIDE.md) lists its four texts |
| `TERMINAL` | Columns, rows, text rows and the color scheme |
| `MEMORY` | The kernel heap: total, used, free, largest free block, blocks |

```
$ SHOW DEVICES DOOR1
```

## HELP

Format: `HELP [topic]`. Prints `SYS$HELP:<verb>.HLP`, the file the command table names for the verb. A topic may be shortened as a verb. The files are on the card in `/UOS/SYS/HELP`, with the 8.3 names `DIRECTOR.HLP` and `AUTHORIZ.HLP` for the long verbs. Messages: `%HELP-W-NOHELP`, `%DCL-W-ABVERB`.

## RUN

Format: `RUN program`, one of `HACK`, `TERMINAL`, `HOLOTAPE`. Starts a program another task registered with `sh_program_register()`. All three are installed. At the prompt, `RUN` alone asks `_Program:` until it gets a name, and Ctrl-C drops the command. From `sh_run_line()`, with no prompt, it gives `%DCL-W-INSFPRM`. Another name gives `%DCL-W-IVKEYW`. A program with no entry would give `%UOS-W-NOTINST, <NAME> is not installed in this build`. A program that fails to start gives `%UOS-E-PROGERR`, or `%UOS-E-IVSPEC` when its argument is not a valid name. Ctrl-C ends the wait for a program; if that program ends later, its result is ignored, also when another `RUN` waits by then.

`RUN TERMINAL` opens the terminal menu of `TERMINAL.CFG` in your home over the shell. The command ends at once with success, so the menu's own command entries can run. BACK at the top level asks `Close the menu?`, and ENTER there brings the `$` prompt back. A `logout` entry still logs out. Without `TERMINAL.CFG` the menu shows `File TERMINAL.CFG not found`. `RUN TERMINAL` from a menu's command entry, while the menu is open, gives `%UOS-E-PROGERR`. See [tui](modules/tui.md#the-screen).

`RUN HOLOTAPE` opens the holotape reader over the shell, on the list of tapes, as the menu's holotape entry does. The command ends at once with success. BACK on the list of tapes closes the reader and brings the `$` prompt back. With the reader already open it gives `%UOS-E-PROGERR`. See [holo](modules/holo.md#the-screen).

`RUN HACK` opens the password game's screen over the shell, the same screen F2 opens at the login. The command ends at once with success. The game plays only at the login prompt with nobody logged in, so from a session it shows the session's refusal, `busy`, and nothing else: no game, no win, no lockout. BACK closes it and brings the `$` prompt back. See [hack](modules/hack.md#the-screen).

## LOGIN

Format: `LOGIN [username]`. Ends the session and shows the login prompt. Without a name the prompt asks `USERNAME:` as after `LOGOUT`. With a name the prompt skips `USERNAME:` and asks only `PASSWORD:` for that user. The login's rules apply as if the name had been typed: a wrong password or an unknown user counts toward the lockout and shows `ACCESS DENIED.`, Ctrl-C shows `LOGIN CANCELLED.`, and both bring `USERNAME:` back. A name that breaks the name rules, 1 to 16 of `A-Z 0-9 $ _`, gives `%UAF-E-IVNAME` and the session goes on.

## LOGOUT

Format: `LOGOUT`. Ends the session with reason `user` and shows the login prompt.

## ROBCO

Format: `ROBCO`. Shows the system's notice: four lines of original text.

## DEVICE

Format: `DEVICE name [action]`. With an action, writes the action word to `DEV:<name>`; then prints the device's status line, read from `DEV:<name>`. The shell never calls the pin code: the device service applies each device's own privilege mask. Messages: `%UOS-E-NODEV`, `%UOS-E-NOPRIV`, `%DEVICE-E-IVACT` for a word the device does not take, `%DEVICE-E-SENSOR` for a command to a sensor, `%DEVICE-E-DEVERR` for a failed write.

```
$ DEVICE LIGHT1 ON
LIGHT1   LIGHT  GPIO25  ON     Hall lights
```

## HOLOTAPE

Format: `HOLOTAPE [tape]`. Runs the `HOLOTAPE` program, as `RUN HOLOTAPE` does. A tape name is 1 to 8 of `A-Z 0-9 $ _ -`, any case, then nothing or `.HTP`. Any other name, a longer one among them, gives `%UOS-E-IVSPEC` and opens nothing: a name is never cut to fit. With a valid name the reader opens that tape once its scan ends; a tape the list does not hold leaves the list shown with `No holotape NAME.HTP`. A second parameter gives `%DCL-W-MAXPARM`.

## AUTHORIZE

Format: `AUTHORIZE keyword`. Needs `SECURITY`; `SYSTEM` grants it.

| Keyword | Does |
|---|---|
| `LIST [user]` | Name, home, privileges and flags. Never a salt or a hash. |
| `ADD user` | Adds a user. Asks `Password:` and `Verification:`, masked. The home defaults to the user name; a name longer than 8 characters needs `/HOME`. The shell then makes `SD0:[HOME.<home>]` with the caller's rights and reports a refusal without undoing the add. A folder that is there already becomes the home as it is: `%UAF-I-HOMEUSED`. A file of that name is a refusal: `%UOS-E-EXISTS`. |
| `MODIFY user` | Changes the qualifiers given. A new `/HOME` moves no folder: `%UAF-I-NOMOVE` names the old one, then the new folder is made as `ADD` makes it. |
| `REMOVE user` | Removes a user. The logged-in user cannot remove itself: `%UAF-E-SELF`. The home folder stays on the card: `%UAF-I-HOMEKEPT`. |

Qualifiers of `ADD` and `MODIFY`:

| Qualifier | Does |
|---|---|
| `/PASSWORD` | Asks for a new password twice, masked. It takes no value: a password is never typed on the command line, where the history, the screen and the echo would keep it. |
| `/PRIVS=(p,...)`, neg | `SYSTEM`, `OPER`, `SECURITY`, `READALL`; `/NOPRIVS` for none |
| `/HOME=folder` | 1 to 8 of `A-Z 0-9 $ _ -` |
| `/DISABLED`, neg | The user cannot log in |
| `/HACKABLE`, neg | `/NOHACKABLE` sets `NOHACK`: never a target of the password game |

Every change builds a new database in memory, writes it with `vfs_sys_write_all()`, the library's atomic write, and calls `sess_reload()`. Changes apply at the user's next login. A `REMOVE`, `/PRIVS`, `/NOPRIVS` or `/DISABLED` that would leave no enabled user with `SYSTEM` or `SECURITY` is refused with `%UAF-E-LASTADMIN` and writes nothing. A file that has no such user already stays as it is. Messages: `%UAF-S-ADDED`, `%UAF-S-MODIFIED`, `%UAF-S-REMOVED`, `%UAF-E-EXISTS`, `%UAF-E-FULL`, `%UAF-E-IVNAME`, `%UAF-E-IVHOME`, `%UAF-E-IVPWD`, `%UAF-E-LASTADMIN`, `%UAF-I-HOMEKEPT`, `%UAF-I-HOMEUSED`, `%UAF-I-NOMOVE`, `%UOS-E-NOUSER`, `%SET-E-PWDNOTVER`. The three `I` lines come before the success line.

```
$ AUTHORIZE ADD BOB /PRIVS=(OPER) /NOHACKABLE
Password: *********
Verification: *********
%UAF-S-ADDED, user BOB added
```
