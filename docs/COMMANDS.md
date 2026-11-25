# UOS commands

This is the reference of every command of the UOS shell. The command table in `src/dcl/tables.c` defines the verbs, keywords and qualifiers; [dcl](modules/dcl.md) has the syntax rules. The shell is described in [ARCHITECTURE.md](ARCHITECTURE.md#the-shell). Nothing is validated on real hardware.

## How commands read

- A verb may be shortened to any unique prefix: `DIR` is `DIRECTORY`, `SH DEV` is `SHOW DEVICES`.
- A qualifier starts with `/`. A qualifier marked "neg" also takes the `/NO` form.
- A line that ends with `-` continues on the next line, at the `_$` prompt.
- A missing parameter is asked for at a `_<name>:` prompt. An empty answer asks again. Ctrl-C drops the command.
- File specifications are those of [vfs path](modules/vfs-path.md): `SD0:[HOME.ADMIN]A.TXT`, `[.SUB]`, `SYS$LOGIN:`, `SYS$HELP:`, `BOOT:`, `DEV:`. `*` matches any characters, `%` one.
- Each file of a wildcard is checked against the access table again, so `DELETE *.*` never deletes `SYSUAF.DAT` and never touches a file the user may not delete.
- Ctrl-C stops a command that runs in steps: a copy, a wildcard loop, a listing, a long `TYPE`.
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
| `SET HALT` | Partial: `/MAINTENANCE` is refused, see below |
| `SHOW` | Full. `SHOW USERS` lists the one session of this terminal. |
| `HELP` | Full |
| `RUN` | Partial: no program is installed in this build. Another task registers them. |
| `LOGIN` | Partial: the user name parameter is not passed to the login prompt |
| `LOGOUT` | Full |
| `ROBCO` | Full |
| `DEVICE` | Full |
| `HOLOTAPE` | Not installed: it runs the `HOLOTAPE` program, which this build lacks |
| `MAIL` | Not installed |
| `AUTHORIZE` | Full |

## DIRECTORY

Format: `DIRECTORY [file-spec]`

Lists a folder, or the files that match a wildcard. With no parameter, the current folder. The listing shows at most 256 entries and says how many more there are.

| Qualifier | Does |
|---|---|
| `/SIZE`, neg | The size in bytes |
| `/DATE`, neg | The FAT date and time |
| `/FULL`, neg | Size and date |
| `/OUTPUT=file` | Writes the listing to a file, default type `.LIS` |

Privileges: none; the access table decides which folders a user may list.

Messages: `%UOS-W-NOFILES` when nothing matches, `%UOS-E-DNF`, `%UOS-E-NOPRIV`, `%UOS-E-IVSPEC`.

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
| `/CONFIRM`, neg | `<file>, copy? [N]:` per file. `Y`, `YES`, `T`, `1` copy; `N`, an empty line or `F` skip; `A` copies this file and the rest; `Q` or Ctrl-C stops. Anything else asks again. |

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

Messages: `%DELETE-S-DELETED, N files deleted`, `%UOS-W-NOFILES`, `%UOS-E-NOPRIV` or `%UOS-E-PROTECT` when every match was refused.

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
- `/MAINTENANCE` asks the boot agent for the maintenance image. `BOOT_PROTOCOL.md` reserves `boot_flags` bit 1 for this, but the boot info is built again on every cold boot, and a reset is a cold boot for the first stage: nothing can carry the request across. So the shell refuses it with `%UOS-E-NOMAINT` and does not restart. `/NOMAINTENANCE` is the plain form.

```
$ SET HALT RESTART
SYSTEM RESTARTING.
```

## SHOW

Format: `SHOW keyword [/OUTPUT=file]`. `/OUTPUT` writes the text to a file.

| Keyword | Shows |
|---|---|
| `DEFAULT` | The current folder |
| `USERS` | The session of this terminal: user, terminal, home, privileges |
| `SYSTEM` | Version, system name, uptime, card state, devices, users |
| `TIME` | The date from the clock chip. With no chip, the uptime and `%UOS-W-NOCLOCK`. |
| `DEVICES [name]` | The device header and one status line per device, read through `DEV:` |
| `TERMINAL` | Columns, rows, text rows and the color scheme |
| `MEMORY` | The kernel heap: total, used, free, largest free block, blocks |

```
$ SHOW DEVICES DOOR1
```

## HELP

Format: `HELP [topic]`. Prints `SYS$HELP:<verb>.HLP`, the file the command table names for the verb. A topic may be shortened as a verb. The files are on the card in `/UOS/SYS/HELP`, with the 8.3 names `DIRECTOR.HLP` and `AUTHORIZ.HLP` for the long verbs. Messages: `%HELP-W-NOHELP`, `%DCL-W-ABVERB`.

## RUN

Format: `RUN program`, one of `HACK`, `TERMINAL`, `HOLOTAPE`. Starts a program another task registered with `sh_program_register()`. With no entry: `%UOS-W-NOTINST, HACK is not installed in this build`. A program that fails to start gives `%UOS-E-PROGERR`.

## LOGIN

Format: `LOGIN [username]`. Ends the session and shows the login prompt. The user name is not passed on: type it at the prompt.

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

Format: `HOLOTAPE [tape [entry]]`. Runs the `HOLOTAPE` program with the parameters. Not installed in this build.

## MAIL

Format: `MAIL keyword`. Not installed: `%UOS-W-NOTINST, MAIL is not installed in this build`. Sending needs a write into another user's home, which the access table refuses, so mail needs a system-side spool first.

## AUTHORIZE

Format: `AUTHORIZE keyword`. Needs `SECURITY`; `SYSTEM` grants it.

| Keyword | Does |
|---|---|
| `LIST [user]` | Name, home, privileges and flags. Never a salt or a hash. |
| `ADD user` | Adds a user. Asks `Password:` and `Verification:`, masked. The home defaults to the user name; a name longer than 8 characters needs `/HOME`. The shell then makes `SD0:[HOME.<home>]` with the caller's rights and reports a refusal without undoing the add. |
| `MODIFY user` | Changes the qualifiers given |
| `REMOVE user` | Removes a user. The logged-in user cannot remove itself: `%UAF-E-SELF`. |

Qualifiers of `ADD` and `MODIFY`:

| Qualifier | Does |
|---|---|
| `/PASSWORD` | Asks for a new password twice, masked. It takes no value: a password is never typed on the command line, where the history, the screen and the echo would keep it. |
| `/PRIVS=(p,...)`, neg | `SYSTEM`, `OPER`, `SECURITY`, `READALL`; `/NOPRIVS` for none |
| `/HOME=folder` | 1 to 8 of `A-Z 0-9 $ _ -` |
| `/DISABLED`, neg | The user cannot log in |
| `/HACKABLE`, neg | `/NOHACKABLE` sets `NOHACK`: never a target of the password game |

Every change builds a new database in memory, writes it with `vfs_sys_write_all()`, the library's atomic write, and calls `sess_reload()`. Changes apply at the user's next login. Messages: `%UAF-S-ADDED`, `%UAF-S-MODIFIED`, `%UAF-S-REMOVED`, `%UAF-E-EXISTS`, `%UAF-E-FULL`, `%UAF-E-IVNAME`, `%UAF-E-IVHOME`, `%UAF-E-IVPWD`, `%UOS-E-NOUSER`, `%SET-E-PWDNOTVER`.

```
$ AUTHORIZE ADD BOB /PRIVS=(OPER) /NOHACKABLE
Password: *********
Verification: *********
%UAF-S-ADDED, user BOB added
```
