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
