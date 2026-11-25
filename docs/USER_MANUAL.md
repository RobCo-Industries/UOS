# UOS user manual

This manual is for the person at the terminal. Nothing in UOS is validated on real hardware yet: what follows is how it behaves in the QEMU emulator.

## Logging in

### What you see

After the boot banner the terminal shows its login screen:

```
ROBCO UNIFIED OPERATING SYSTEM - SECURE ACCESS NODE
TERMINAL: ROBCO TERMINAL
--------------------------------------------------
AUTHORISED PERSONNEL ONLY. EVERY ACCESS ATTEMPT IS RECORDED.
IDENTIFY YOURSELF TO PROCEED.


USERNAME: _
```

Type your user name and press Enter. Then type your password at `PASSWORD:` and press Enter. The password shows as one `*` per character. Case matters in the password, not in the user name.

The terminal then shows `CHECKING CREDENTIALS...` for a moment. After a good login it greets you by name, lists your privileges and opens your session at the command line, see [Using the command line](#using-the-command-line). Type `LOGOUT` to leave. The login screen comes back with `SESSION CLOSED.`

The terminal's owner can set a session to close by itself after some time with no key. Then the login screen comes back the same way.

### Mistakes

- A wrong user name or a wrong password gives the same message: `ACCESS DENIED.` with the number of attempts left. The terminal never tells which one was wrong.
- Ctrl-C at `USERNAME:`, or Enter on an empty name, asks for the name again. Nothing is counted.
- Ctrl-C at `PASSWORD:` shows `LOGIN CANCELLED.` and asks for the name again. Nothing is counted.
- Backspace, Delete and Esc erase the character before the cursor.

### Lockout

After 3 failed logins in a row, the terminal locks for 30 seconds. The owner can change both numbers. The screen shows `TERMINAL LOCKED: TOO MANY FAILED LOGINS.` and counts down once a second. Every key is ignored until then. Then the login screen comes back with `TERMINAL UNLOCKED. YOU MAY TRY AGAIN.` and the count starts again.

A good login also starts the count again.

The terminal keeps the lock in memory only. A restart clears it.

### Default accounts

A new card has three accounts:

| User | Password | Can |
|---|---|---|
| `ADMIN` | `ADMIN-CHANGE-ME` | everything |
| `OPER` | `OPER-CHANGE-ME` | run the devices |
| `GUEST` | `GUEST` | read its own files |

This manual prints these passwords, so anyone can read them. Change them before the terminal guards anything: log in as each user and type `SET PASSWORD`, see [Changing your password](#changing-your-password).

### Buttons and the on-screen keyboard

A terminal with buttons and no keyboard works too. At `USERNAME:` or `PASSWORD:`, press any button: the on-screen keyboard opens over the bottom of the screen. That first press does nothing else.

- The arrow buttons move the highlight.
- SELECT types the highlighted key.
- SHIFT switches between capitals and digits, and small letters and signs.
- BKSP erases, LEFT and RIGHT move in the line.
- ENTER sends the line and closes the keyboard.
- CLOSE, or BACK, closes the keyboard and keeps the line.

The password shows as `*` here too.

### When there is no login

If the terminal cannot log anyone in, it shows the system screen instead, with two lines that say why and what to do:

| The screen says | Meaning | What to do |
|---|---|---|
| `USER DATABASE MISSING.` | The card has no `/UOS/SYSUAF.DAT` | Copy the file from a backup |
| `USER DATABASE DAMAGED (...)` | The file is unreadable or broken | Copy the file from a backup |
| `LOGIN IS NOT AVAILABLE WITHOUT A UOS CARD.` | No card | Insert a UOS card |
| `LOGIN IS NOT AVAILABLE: NO /UOS FOLDER.` | The card has no `/UOS` folder | Insert a UOS card |

Then press F4 to restart the terminal.

## Using the command line

After you log in, the terminal shows a line about the shell and the prompt `$`. Type a command and press Enter. `HELP` lists the commands; `HELP DIRECTORY` explains one. [COMMANDS.md](COMMANDS.md) is the full reference.

```
UOS V0.1.0 command shell. Type HELP for the commands.
$ DIRECTORY
Directory SD0:[HOME.GUEST]

NOTICE.TXT
TERMINAL.CFG
VISITOR.TXT

Total of 3 files.
$ TYPE NOTICE.TXT
```

### Typing commands

- You may shorten a command: `DIR` for `DIRECTORY`, `SH DEF` for `SHOW DEFAULT`. Upper and lower case are the same.
- Options start with a slash: `DIR /SIZE`. Most take a `/NO` form: `/NOLOG`.
- UP and DOWN bring back earlier lines. LEFT and RIGHT move in the line. Backspace erases.
- Ctrl-C clears the line you are typing. While a command runs, Ctrl-C stops it and brings back the prompt, with `%UOS-I-CANCEL`. This works everywhere: in a long copy or listing, in the `TYPE /PAGE` viewer, at a yes or no question, at a password, while you type `CREATE` lines, with the on-screen keyboard open, and in a program. Files already done stay; a file in the middle is not left half-written.
- A line that ends with `-` goes on: the next prompt is `_$`.
- If you leave out something a command needs, it asks: `TYPE` alone asks `_File:`. Press Ctrl-C to drop the command.
- Messages look like `%UOS-E-FNF, file not found`. The letter after the first dash says how it went: `S` done, `I` for your information, `W` a warning, `E` an error.

### Files and folders

Your files are in your home folder, `SD0:[HOME.<you>]`. `SYS$LOGIN:` is a short name for it. `SHOW DEFAULT` says where you are; `SET DEFAULT [.LOGS]` goes into a folder, `SET DEFAULT [-]` goes back up.

| To | Type |
|---|---|
| List files | `DIRECTORY`, `DIR *.TXT /SIZE` |
| Read a file | `TYPE NOTICE.TXT`, or `TYPE NOTICE.TXT /PAGE` to read it a screen at a time |
| Write a new file | `CREATE NOTE.TXT`, then the lines, then a line with only `.` |
| Make a folder | `CREATE [.LOGS] /DIRECTORY` |
| Copy | `COPY NOTE.TXT [.LOGS]` |
| Rename or move | `RENAME NOTE.TXT OLDNOTE.TXT` |
| Delete | `DELETE OLDNOTE.TXT`, or `DELETE *.TXT /CONFIRM` to be asked for each file |

`*` in a name matches anything, `%` one character. With `/CONFIRM`, answer `Y` for yes, `N` or Enter for no, `A` for this file and all the rest, `Q` to stop.

You can read and change your own files only. Other homes and the system files are closed to you unless your account has the privileges for them. The user database, `SYSUAF.DAT`, can only change through `SET PASSWORD` and `AUTHORIZE`.

### Changing your password

Type `SET PASSWORD`. The terminal asks for your old password, the new one, and the new one again. Each shows as `*`. A password is 1 to 32 characters. The change works from your next login.

### Devices

`SHOW DEVICES` lists the doors, lights and sensors the terminal controls, with their states. `DEVICE LIGHT1 ON` switches one; `DEVICE DOOR1` shows one. Each device decides who may use it: a refused command says `%UOS-E-NOPRIV`.

### Other commands

- `SHOW SYSTEM`, `SHOW TIME`, `SHOW USERS`, `SHOW TERMINAL`, `SHOW MEMORY` give the terminal's state.
- `SET TERMINAL /SCHEME=AMBER` changes the colors: `GREEN`, `AMBER`, `BLUE` or `WHITE`, until the next restart.
- `LOGOUT` ends your session.
- An operator can type `SET HALT` to stop the terminal, or `SET HALT RESTART` to restart it.
- A security officer manages the users with `AUTHORIZE`: `AUTHORIZE LIST`, `AUTHORIZE ADD BOB`, `AUTHORIZE MODIFY BOB /DISABLED`, `AUTHORIZE REMOVE BOB`.
- `RUN`, `HOLOTAPE` and `MAIL` are not part of this build yet: they say so.
