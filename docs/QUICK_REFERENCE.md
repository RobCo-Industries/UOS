# UOS quick reference

On a serial keyboard, Enter is SELECT. Backspace, Delete and a lone Esc are BACK.

## Commands

A verb, keyword or qualifier may be cut to any prefix that names only it. The table gives the shortest one. `tests/host/test_dcl_table.c` checks every one of them.

| Command | Shortest | Does | Detail |
|---|---|---|---|
| `AUTHORIZE` | `A` | Lists, adds, changes and removes users. Needs `SECURITY`. | [COMMANDS](COMMANDS.md#authorize) |
| `COPY` | `CO` | Copies files. Never replaces one. | [COMMANDS](COMMANDS.md#copy) |
| `CREATE` | `CR` | Makes a file from typed lines, or a folder with `/DIRECTORY` | [COMMANDS](COMMANDS.md#create) |
| `DELETE` | `DEL` | Deletes files | [COMMANDS](COMMANDS.md#delete) |
| `DEVICE` | `DEV` | Shows a device, or acts on it: `DEV LIGHT1 ON` | [COMMANDS](COMMANDS.md#device) |
| `DIRECTORY` | `DI` | Lists files | [COMMANDS](COMMANDS.md#directory) |
| `HELP` | `HE` | Prints the help of a verb | [COMMANDS](COMMANDS.md#help) |
| `HOLOTAPE` | `HO` | Opens the holotape reader, or one tape | [COMMANDS](COMMANDS.md#holotape) |
| `LOGIN` | `LOGI` | Ends the session and shows the login. `LOGI GUEST` asks only for GUEST's password. | [COMMANDS](COMMANDS.md#login) |
| `LOGOUT` | `LOGO` | Ends the session | [COMMANDS](COMMANDS.md#logout) |
| `RENAME` | `RE` | Renames or moves files | [COMMANDS](COMMANDS.md#rename) |
| `ROBCO` | `RO` | Shows the system's notice | [COMMANDS](COMMANDS.md#robco) |
| `RUN` | `RU` | Runs `HACK` (`HA`), `TERMINAL` (`T`) or `HOLOTAPE` (`HO`) | [COMMANDS](COMMANDS.md#run) |
| `SET` | `SE` | `DEFAULT` (`D`), `PASSWORD` (`P`), `TERMINAL` (`T`), `HALT` (`H`) | [COMMANDS](COMMANDS.md#set) |
| `SHOW` | `SH` | `DEFAULT` (`DEF`), `DEVICES` (`DEV`), `MEMORY` (`M`), `SYSTEM` (`S`), `TERMINAL` (`TE`), `TIME` (`TI`), `USERS` (`U`) | [COMMANDS](COMMANDS.md#show) |
| `TYPE` | `T` | Shows a text file. `/PAGE` opens the pager. | [COMMANDS](COMMANDS.md#type) |

`AUTHORIZE` keywords: `ADD` (`A`), `LIST` (`L`), `MODIFY` (`M`), `REMOVE` (`R`). Its qualifiers: `/DISABLED` (`/D`), `/HACKABLE` (`/HA`), `/HOME` (`/HO`), `/PASSWORD` (`/PA`), `/PRIVS` (`/PR`).

A qualifier marked "neg" in [COMMANDS](COMMANDS.md) also takes `/NO`, such as `/NOLOG`. A line that ends with `-` goes on at `_$`. Ctrl-C stops the running command with `%UOS-I-CANCEL, operation cancelled`.

## Keys

### Line editor

At `$`, `USERNAME:`, `PASSWORD:` and every question. Detail: [USER_MANUAL](USER_MANUAL.md#typing-commands).

| Key | Does |
|---|---|
| Enter | Sends the line |
| LEFT, RIGHT, Ctrl-B, Ctrl-F | Move the cursor |
| F1, Ctrl-A | Start of the line |
| F2, Ctrl-E | End of the line |
| BACK | Erases the character before the cursor |
| F3, Ctrl-D | Erases the character under the cursor |
| Ctrl-K | Erases to the end of the line |
| Ctrl-O | Switches insert and overwrite |
| UP, DOWN, Ctrl-P, Ctrl-N | Earlier and later lines, at `$` and `_$` only |
| Ctrl-C | Clears the line, or stops the command |
| F4 | Opens the on-screen keyboard |
| F2 on an empty `USERNAME:` | Starts the password game |

### On-screen keyboard

Detail: [USER_MANUAL](USER_MANUAL.md#buttons-and-the-on-screen-keyboard).

| Key | Does |
|---|---|
| Arrows | Move the highlight |
| SELECT | Types the highlighted key |
| SHIFT key | Capitals and digits, or small letters and signs |
| BKSP, LEFT, RIGHT keys | Erase, move in the line |
| ENTER key | Sends the line and closes |
| CLOSE key, BACK | Closes and keeps the line |

### Menu

Detail: [USER_MANUAL](USER_MANUAL.md#the-terminal-menu).

| Key | Does |
|---|---|
| UP, DOWN | Move the highlight |
| 1 to 9 | Go to that entry |
| Enter, SELECT | Choose the entry |
| BACK, LEFT | Up one menu. At the top: `Log out?` |

### Pager

`TYPE /PAGE`, menu text entries and holotape entries.

| Key | Does |
|---|---|
| UP, DOWN, `k`, `j` | One line |
| LEFT, F1, `b` | Page up |
| RIGHT, SELECT, F2, Space | Page down |
| F3, `g` | Top |
| F4, `G` | End |
| BACK, `q` | Leave |

### Holotape reader

Detail: [USER_MANUAL](USER_MANUAL.md#reading-holotapes).

| Key | Does |
|---|---|
| UP, DOWN | Move the `>` mark |
| SELECT, RIGHT | Open the tape or the entry |
| BACK, LEFT | Back one step. On the tape list: leave. |

### Password game

Detail: [USER_MANUAL](USER_MANUAL.md#the-password-game).

| Key | Does |
|---|---|
| Arrows | Move the highlight |
| SELECT | Try the word, or use the bracket pair |
| BACK | Leave. Nothing is counted. |

## Paths

| Name | On the card | Detail |
|---|---|---|
| `SYS$LOGIN:` | `/UOS/HOME/<home>`, your home | [vfs-path](modules/vfs-path.md#devices-and-logical-names) |
| `SD0:` | `/UOS` | |
| `SYS$HELP:` | `/UOS/SYS/HELP` | |
| `SYS$HOLO:` | `/UOS/HOLO`, the holotapes | |
| `SYS$SYSTEM:` | `/UOS/SYS` | |
| `BOOT:` | `/ROBCO`, read only | |
| `DEV:` | The devices | |

`[.LOGS]` is a folder in the current one, `[-]` the folder above. `*` matches any characters, `%` one.

## Login

Detail: [USER_MANUAL](USER_MANUAL.md#logging-in) and [ADMIN_GUIDE](ADMIN_GUIDE.md#login-rules).

- A user name is 1 to 16 of `A-Z 0-9 $ _`, any case. A password is 1 to 32 characters, and case matters.
- A wrong name and a wrong password both give `ACCESS DENIED.`
- 3 failures in a row lock the terminal for 30 seconds by default. A good login clears the count. A restart clears the lock.
- A lost password game locks it for 60 seconds by default.
- Default users: `ADMIN`, `OPER`, `GUEST`. Change their passwords.
