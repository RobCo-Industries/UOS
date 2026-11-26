# UOS user manual

This manual is for the person at the terminal. Nothing in UOS is validated on real hardware yet: what follows is how it behaves in the QEMU emulator.

The [quick reference](QUICK_REFERENCE.md) fits on two screens. The person who prepares the card reads the [administrator guide](ADMIN_GUIDE.md).

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

The terminal then shows `CHECKING CREDENTIALS...` for a moment. After a good login it greets you by name, lists your privileges and opens your session. When your home holds a menu file, the session is the terminal menu, see [The terminal menu](#the-terminal-menu). Otherwise it is the command line, see [Using the command line](#using-the-command-line). Type `LOGOUT` to leave. The login screen comes back with `SESSION CLOSED.`

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
| `OPER` | `OPER-CHANGE-ME` | run the devices. Gets the terminal menu after the login |
| `GUEST` | `GUEST` | read its own files |

This manual prints these passwords, so anyone can read them. Change them before the terminal guards anything: log in as each user and type `SET PASSWORD`, see [Changing your password](#changing-your-password).

### Buttons and the on-screen keyboard

At `USERNAME:` or `PASSWORD:`, press F4 to open the on-screen keyboard over the bottom of the screen. It is meant for a terminal with buttons and no keyboard, but UOS does not read GPIO buttons yet, so today F4 is the only way to open it. That key does nothing else.

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
VISITOR.TXT

Total of 2 files.
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
- An operator can type `SET HALT` to stop the terminal, or `SET HALT RESTART` to restart it. `SET HALT RESTART /MAINTENANCE` restarts into the boot agent's maintenance screen. From there the maintenance image can restore the default users when every password is lost, if the card's `BOOT.CFG` allows it. If the request cannot be saved, the terminal says so and stays up.
- A security officer manages the users with `AUTHORIZE`: `AUTHORIZE LIST`, `AUTHORIZE ADD BOB`, `AUTHORIZE MODIFY BOB /DISABLED`, `AUTHORIZE REMOVE BOB`.
- `RUN HOLOTAPE` or `HOLOTAPE` opens the holotape reader, see [Reading holotapes](#reading-holotapes). `HOLOTAPE STORY` opens one tape.
- `RUN HACK` opens the password game's screen. The game plays only at the login prompt, so from a session it tells you why it cannot start. BACK closes it.
- `LOGIN GUEST` ends your session and asks for GUEST's password. `LOGIN` alone shows the login prompt.

## The terminal menu

A user whose home holds `TERMINAL.CFG` gets a menu after the login instead of the command line. On a new card that is OPER.

```
 ROBCO TERMINAL
 Operator console

 Operator console. Walk the rounds at the start of every shift.

 > Read the rounds checklist
   Device controls
   Show the system
   Yard lights on or off
   Holotape library
   Log out


 Operator console
 UP/DOWN: move  1-9: pick  ENTER: choose  BACK: log out
```

The first row is the terminal's name, the second the menu's title. Lines frame the header and the entries. Under them are the message row, the trail of menus you came through, and the keys.

- UP and DOWN move the highlight. Held, they keep moving.
- The digits 1 to 9 move to that entry.
- ENTER or SELECT chooses the entry.
- BACK or LEFT goes up one menu. At the top it asks `Log out?`: ENTER logs out, BACK stays.

Buttons work the same way as keys.

What each kind of entry does:

| Entry | What happens |
|---|---|
| a text | The file opens in the pager. BACK returns to the menu. |
| a menu | The other menu opens. Menus go 8 deep at most. |
| a command | The command runs as you. Its output opens in the pager, and its last message shows on the message row. A question, such as `/CONFIRM`, opens over the menu. Ctrl-C cancels it. |
| a device | The device acts. The message row shows its new state, or why it refused. |
| a holotape | The holotape reader opens on that tape, or on the list. BACK returns. |
| log out | Ends the session at once. |

When something goes wrong, the message row says so in one line, such as `File DUTY.TXT not found`, `Bad menu file TOOLS.CFG (line 4)` or `No holotape STORY.HTP`. The menu stays usable.

The menu is your whole session. You reach the command line only through a command entry that the menu offers.

### Writing a menu

Writing a menu is in the [administrator guide](ADMIN_GUIDE.md#writing-a-menu).

## Reading holotapes

A holotape is a file on the terminal's card, in the folder `SYS$HOLO`. Each tape has a title and a few entries of text: logs, procedures, letters.

Open the reader with `RUN HOLOTAPE` or `HOLOTAPE` at the `$` prompt, or with a holotape entry of your menu. `HOLOTAPE STORY` opens the tape `STORY` at once. A tape name is 1 to 8 letters, digits, `$`, `_` or `-`, then nothing or `.HTP`. Another name gives `%UOS-E-IVSPEC`. When the reader closes, the prompt or the menu comes back.

The holotape reader lists the tapes by file name and title. Use UP and DOWN to move the `>` mark, and SELECT to open a tape. The terminal shows `LOADING` with the tape's name while it reads. A large tape can take a few seconds.

An open tape lists its entries by number and name. SELECT shows an entry. In the entry, UP and DOWN move one line, LEFT and RIGHT one page. Press BACK to go back to the list of entries, BACK again to close the tape, and BACK on the list of tapes to leave the reader.

A held UP or DOWN keeps moving, and a held arrow keeps paging an entry. A held SELECT or BACK acts once: press it again for the next step.

What the reader can tell you:

| The screen says | Meaning |
|---|---|
| `DAMAGED:` and a reason, in place of a title | The tape is broken. It cannot be opened |
| `NOT CHECKED: select it to try again` | The reader could not read the tape when it opened, for example for lack of memory. Select it to try again |
| `THIS HOLOTAPE COULD NOT BE READ NOW. TRY AGAIN.` | The card did not answer. Try again; if it goes on, check the card |
| `No holotapes found in SYS$HOLO.` | The folder is empty |
| `This card has no holotape folder, SYS$HOLO.` | The card has no tape folder |
| `No card. Insert a UOS card to read holotapes.` | No card is in the terminal |
| `Holotapes in SYS$HOLO: 40, first 32 shown` | The reader lists 32 tapes at most |
| `NOT ENOUGH MEMORY TO LOAD THIS HOLOTAPE.` | Close other work and try again |

The list scrolls when it is longer than the screen. Any user may read the tapes.

## The password game

The terminal keeps a maintenance view of its memory. With some patience you can find a password in it and log in without knowing it. The game logs you in as one account the owner chose, by default `GUEST`, an account with no privileges.

### Starting

At `USERNAME:`, leave the line empty and press F2. Close any other program first: over one, the game answers `CLOSE THE OPEN PROGRAM: THE GAME STARTS FROM THE LOGIN PROMPT.` The game needs a keyboard to start; once it runs, the arrow buttons and SELECT are enough.

### What you see

The screen shows two columns of memory. Each row starts with an address, such as `0x41D7`, then 12 characters. Among the signs are words, all of the same length. One of them is the password. A long word can run past the end of a row and go on at the start of the next row of the same column.

To the right, the terminal keeps notes of what you tried, the newest at the bottom. Above the memory it shows how many attempts you have left.

### Playing

- The arrow keys move the highlight. A word lights up whole.
- SELECT tries the highlighted word.
- BACK leaves the game. Nothing is counted and the login comes back.

A held arrow key keeps moving. A held SELECT or BACK acts once.

A won game clears the count of failed logins, as a right password does.

A wrong word costs one attempt. The terminal shows `Entry denied.` and the likeness.

### Likeness

The likeness is the number of letters of your word that are in the same place in the password. Say the password is `LIST` and you tried `LOST`: `L`, `S` and `T` match in place, so the likeness is 3. Use it to rule words out. A word that does not have that likeness with the word you tried cannot be the password.

### Brackets

Some signs come in pairs on one row: `()`, `[]`, `{}` or `<>`, with no letter between them. Move to the opening sign: the whole pair lights up. SELECT on it does one of two things:

- `Dud removed.`: one wrong word turns into dots and can no longer be chosen.
- `Tries reset.`: your attempts go back to the full count.

Each pair works once. The password is never removed.

### Winning and losing

The right word shows `Exact match.` and `PASSWORD ACCEPTED.`. A moment later the terminal greets you and opens the session.

When your last attempt fails, the terminal shows `NO ATTEMPT LEFT. THE TERMINAL LOCKS.` Then it locks, as after too many failed logins, with `TERMINAL LOCKED: PASSWORD RECOVERY FAILED.` and a countdown, by default 60 seconds. Every key is ignored until then. Then the login comes back. A restart also clears the lock.

### When the game does not start

| The screen says | Meaning |
|---|---|
| `THE PASSWORD GAME IS TURNED OFF ON THIS TERMINAL.` | The owner turned it off |
| `THE TARGET ACCOUNT CANNOT BE HACKED.` | The owner protected that account |
| `THE PASSWORD GAME HAS NO TARGET ACCOUNT.` | The account does not exist |
| `THE TARGET ACCOUNT IS DISABLED.` | The account is turned off |
| `WORD LIST ... IS MISSING.` or `IS DAMAGED.` | The card's word lists are not usable |

Press BACK to go back to the login.
