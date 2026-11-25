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

The terminal then shows `CHECKING CREDENTIALS...` for a moment. After a good login it greets you by name, lists your privileges and opens your session. This build has no shell yet: the session screen says `No shell is installed in this build.` Press BACK to log out. The login screen comes back with `SESSION CLOSED.`

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

This manual prints these passwords, so anyone can read them. Change them before the terminal guards anything. This build cannot change a password at the terminal yet: the owner builds a new `SYSUAF.DAT` with `tools/mkuaf.py` and copies it to the card.

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

## Reading holotapes

A holotape is a file on the terminal's card, in the folder `SYS$HOLO`. Each tape has a title and a few entries of text: logs, procedures, letters.

The holotape reader lists the tapes by file name and title. Use UP and DOWN to move the `>` mark, and SELECT to open a tape. The terminal shows `LOADING` with the tape's name while it reads. A large tape can take a few seconds.

An open tape lists its entries by number and name. SELECT shows an entry. In the entry, UP and DOWN move one line, LEFT and RIGHT one page. Press BACK to go back to the list of entries, BACK again to close the tape, and BACK on the list of tapes to leave the reader.

What the reader can tell you:

| The screen says | Meaning |
|---|---|
| `DAMAGED:` and a reason, in place of a title | The tape is broken. It cannot be opened |
| `No holotapes found in SYS$HOLO.` | The folder is empty |
| `This card has no holotape folder, SYS$HOLO.` | The card has no tape folder |
| `No card. Insert a UOS card to read holotapes.` | No card is in the terminal |
| `Holotapes in SYS$HOLO: 40, first 32 shown` | The reader lists 32 tapes at most |
| `NOT ENOUGH MEMORY TO LOAD THIS HOLOTAPE.` | Close other work and try again |

The list scrolls when it is longer than the screen. Any user may read the tapes.

## The password game

The terminal keeps a maintenance view of its memory. With some patience you can find a password in it and log in without knowing it. The game logs you in as one account the owner chose, by default `GUEST`, an account with no privileges.

### Starting

At `USERNAME:`, leave the line empty and press F2. The game needs a keyboard to start; once it runs, the arrow buttons and SELECT are enough.

### What you see

The screen shows two columns of memory. Each row starts with an address, such as `0x41D7`, then 12 characters. Among the signs are words, all of the same length. One of them is the password. A long word can run past the end of a row and go on at the start of the next row of the same column.

To the right, the terminal keeps notes of what you tried, the newest at the bottom. Above the memory it shows how many attempts you have left.

### Playing

- The arrow keys move the highlight. A word lights up whole.
- SELECT tries the highlighted word.
- BACK leaves the game. Nothing is counted and the login comes back.

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
