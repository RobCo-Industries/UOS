# Lore and real

UOS takes names and ideas from the terminals of the Fallout games. All text here is original. The two in-world documents in [lore/](lore/) are fiction: [an internal memo](lore/INTERNAL_MEMO_20771023.md) and [a security advisory](lore/SECURITY_ADVISORY_2077.md). This table says what the fiction claims and what UOS really does.

| The fiction says | UOS really does | Where |
|---|---|---|
| UOS v7.0.2.8, deployed on millions of terminals | UOS V0.1.0. It runs in QEMU and is not validated on real hardware. | [README](../README.md#status) |
| `SET HALT RESTART /MAINTENANCE` opens a maintenance screen with no login | True. It needs `OPER`, writes `/ROBCO/MAINT.REQ` and restarts. MFBootAgent then opens its maintenance screen. | [ADMIN_GUIDE](ADMIN_GUIDE.md#maintenance) |
| Anyone at that screen can reset the accounts | Only when `BOOT.CFG` sets `[maint] restore_users = on`. The shipped setting is off. | [ADMIN_GUIDE](ADMIN_GUIDE.md#lost-passwords) |
| Passwords are a salted SHA-256, repeated 2000 times | True on the default card. `tools/mkuaf.py` takes 1 to 100000 rounds. | [session](modules/session.md#hash) |
| The login prompt cannot be overflowed | The prompt takes at most 16 characters for a name and 32 for a password | [session](modules/session.md#the-login-screen) |
| Default accounts ADMIN, OPER and GUEST with printed passwords | True. They are ordinary records of `SYSUAF.DAT` and can be changed or disabled. | [ADMIN_GUIDE](ADMIN_GUIDE.md#users) |
| 3 wrong passwords lock the terminal for 30 seconds | True by default. `UOS.CFG [login]` sets both numbers. | [ADMIN_GUIDE](ADMIN_GUIDE.md#login-rules) |
| No network, only a serial service port | True. Networking is out of scope. | [README](../README.md#out-of-scope) |
| A password crosses the serial port as stars | True | [session](modules/session.md#secrets) |
| Messages name facility, severity and code | True: `%UOS-E-NOPRIV, not enough privilege for this operation` | [COMMANDS](COMMANDS.md#how-commands-read) |
| Holotapes cannot hold a backup | True. A holotape is a read-only text archive. | [ADMIN_GUIDE](ADMIN_GUIDE.md#holotapes) |
| A memory dump hides the password, and a likeness count guides the guess | The password game: it logs in one chosen account, `GUEST` by default | [USER_MANUAL](USER_MANUAL.md#the-password-game) |
| An internal mail system | No `MAIL` command. It was removed. | [CHANGELOG](../CHANGELOG.md) |
| Uptime of 200 years, 256 KB of RAM, a Model 4000 | Fiction. UOS is built for Raspberry Pi boards and runs in QEMU on raspi0, raspi1ap, raspi2b and raspi3b. | [README](../README.md#status) |
