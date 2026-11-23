# Session: users and login

The session module holds the user database, the password hash and the login lockout. The pure files are `src/session/sha256.c`, `src/session/uaf.c` and `src/session/lockout.c`. They do no file access and keep no global state. Nothing here is validated on real hardware.

## Files

| File | Prefix | What |
|---|---|---|
| `sha256.h` | `sha_` | SHA-256, FIPS 180-4, streaming |
| `uaf.h` | `uaf_` | `SYSUAF.DAT` check, load, write, lookup, hash, login check, user edits, privilege names |
| `lockout.h` | `lk_` | login failure count and lockout timer |
| `tools/mkuaf.py` | | builds and checks `SYSUAF.DAT` |

## SYSUAF.DAT layout

Little-endian, fixed width. The file is exactly `32 + count * 96` bytes.

Header, 32 bytes:

| Offset | Size | Field | Value |
|---|---|---|---|
| 0 | 8 | magic | `UOSUAF1` and a NUL |
| 8 | 2 | version | 1 |
| 10 | 2 | record_size | 96 |
| 12 | 2 | count | 0 to 32 |
| 14 | 2 | reserved | 0 |
| 16 | 4 | iterations | 1 to 100000 |
| 20 | 4 | crc32 | CRC-32/ISO-HDLC of all records, in order |
| 24 | 8 | reserved | 0 |

Record, 96 bytes:

| Offset | Size | Field | Rule |
|---|---|---|---|
| 0 | 16 | name | 1 to 16 of `A-Z 0-9 $ _`, upper-case, NUL padded; 16 characters leave no NUL |
| 16 | 16 | salt | any bytes |
| 32 | 32 | hash | the iterated hash below |
| 64 | 4 | privs | privilege bits |
| 68 | 4 | flags | flag bits |
| 72 | 16 | home | folder under `/UOS/HOME`, 1 to 8 of `A-Z 0-9 $ _ -`, upper-case, NUL padded |
| 88 | 8 | reserved | 0 |

`uaf_validate` checks in this order and returns the first failure: length at least 32 (`UAF_E_SHORT`), magic, version, record size, count, header reserved bytes, iterations, exact size, CRC, then each record: name (`UAF_E_NAME`, `UAF_E_NAME_PAD`), home (`UAF_E_HOME`, `UAF_E_HOME_PAD`), reserved bytes, and a name already seen (`UAF_E_DUPLICATE`). A record error also gives the record index. Bits in `privs` and `flags` outside the defined ones are not checked.

## Hash

```
h = SHA-256(salt || password)
repeat iterations - 1 times:
    h = SHA-256(h || salt || password)
```

The salt is 16 bytes. The password is 1 to 32 printable ASCII characters, 0x20 to 0x7E, case-sensitive, with no terminator in the hash input. Iterations come from the header. The tool's default is 2000.

`uaf_verify` always runs the full iteration count. For an unknown user it hashes against a dummy record with a zero salt. For a password that breaks the rules it hashes a dummy 32-character password. It compares all 32 bytes with no early exit. Unknown user and wrong password return the same code, `UAF_E_BAD_LOGIN`. `UAF_E_DISABLED` comes back only after a right password. The optional `uaf_work_t` counts SHA-256 messages and compressions so tests can check the work is the same in every case.

## Cost of one login check

A round hashes `32 + 16 + n` bytes for a password of `n` characters. That fits one 64-byte block for `n` up to 7 and takes two blocks from 8. At 2000 iterations one check takes 2000 compressions for short passwords and 3999 for passwords of 8 or more characters. The host test `worst_case_login_count` checks both numbers.

Estimate for an ARM1176 at 700 MHz, unverified. The cross GCC at `-O2` builds the compression loop as 47 instructions per round for rounds 0 to 15 and 66 for rounds 16 to 63, plus about 280 for loading and storing state. That is about 4200 instructions per compression. Buffer copies and padding add about 1000 instructions per hash. The worst case is about 19 million instructions. With caches on, at about 1.3 cycles per instruction, that is about 35 ms. With caches off, every fetch and load goes to SDRAM; at an assumed 15 to 25 cycles per instruction it is 0.4 to 0.7 s. The default stays 2000 because the estimate is not clearly over half a second. The image should turn on at least the instruction cache. Nobody has measured this in QEMU or on a board yet.

## Privileges and flags

| Bit | Privilege | Meaning |
|---|---|---|
| 0 | `SYSTEM` | everything |
| 1 | `OPER` | device control, `SET HALT` |
| 2 | `SECURITY` | user management, skips the minigame lockout |
| 3 | `READALL` | read every home |

| Bit | Flag | Meaning |
|---|---|---|
| 0 | `DISABLED` | the account cannot log in |
| 1 | `NOHACK` | the user can never be the minigame target |

`uaf_privs_format` writes names in bit order joined by commas, or an empty string for none. It needs `UAF_PRIVS_TEXT_MAX` bytes, 32, for the full set. `uaf_privs_parse` reads a comma list, ignores case and spaces, and accepts optional parentheses. Each name may be shortened to a unique prefix, so `S` is an error and `SY` is `SYSTEM`.

## Public functions

- `sha_init`, `sha_update`, `sha_final`, `sha_digest`: SHA-256.
- `uaf_validate`, `uaf_load`: check an image; load it into a `uaf_db_t` the caller owns.
- `uaf_db_init`, `uaf_write`: start an empty database; write an image with a fresh CRC. A short buffer gives `UAF_E_BUFFER` and writes nothing.
- `uaf_check_name`, `uaf_check_home`, `uaf_check_password`: the rules.
- `uaf_find`: index by name, case-insensitive.
- `uaf_hash`, `uaf_hash_equal`, `uaf_verify`: the hash and the login check.
- `uaf_add`, `uaf_remove`, `uaf_set_password`, `uaf_set_privs`, `uaf_set_flags`, `uaf_set_home`: edits for `AUTHORIZE`. The caller supplies each salt. At most 32 users.
- `uaf_privs_format`, `uaf_privs_parse`, `uaf_priv_name`: privilege text.
- `uaf_strerror`, `lk_strerror`: one line per error code.
- `lk_init`, `lk_fail`, `lk_success`, `lk_locked`, `lk_remaining_s`, `lk_tries_left`: the lockout.

## Lockout rule

`lk_init` takes `retries`, 1 to 9, and `lockout_s`, 0 to 3600, from `UOS.CFG [login]`. Each failed login adds one to the count. When the count reaches `retries`, the login locks for `lockout_s` seconds and the count resets. A failure while locked is ignored. A success clears the count. With `lockout_s = 0` nothing locks. `lk_remaining_s` gives the whole seconds left, rounded up, for the screen.

Ticks are milliseconds in a `uint32_t`, which wraps every 2^32 ms, about 49.7 days. The code only uses `now - start` in 32 bits, so a lock that spans the wrap works. The caller must call an `lk_` function at least once per wrap period while locked. The login screen does that on every tick.

## mkuaf.py

```
python3 tools/mkuaf.py build USERS.INI -o SYSUAF.DAT [--seed TEXT] [--iterations N]
python3 tools/mkuaf.py check SYSUAF.DAT
python3 tools/mkuaf.py vectors -o tests/vectors/uaf.json
python3 tools/mkuaf.py vectors-h tests/vectors/uaf.json -o uaf_vectors.h
```

`USERS.INI` has one section per user. The section name is the user name. Keys: `password` (required), `privs` (a comma list or a number, default none), `home` (default the user name), `flags` (`DISABLED`, `NOHACK` as a list, or a number). Unknown keys are errors. Without `--seed` each salt comes from `os.urandom`. With `--seed` the salt is the first 16 bytes of `SHA-256(seed || 0x00 || NAME)`. `check` validates like `uaf_validate` and lists users, homes, privileges and flags. It never prints salts or hashes.

Exit codes: 0 success, 1 bad input or a bad file with a `mkuaf:` message on stderr, 2 bad usage.

The shared vectors in `tests/vectors/uaf.json` come from `vectors`. The C test reads them through a header that `vectors-h` generates during the build. A header is simpler than a JSON parser in C.

## Default users

`card/UOS/SYSUAF.DAT` is built from `tools/default_users.ini` with `--seed uos-default-card`. The card holds no `USERS.INI`.

| User | Password | Privileges | Flags |
|---|---|---|---|
| `ADMIN` | `ADMIN-CHANGE-ME` | `SYSTEM` | `NOHACK` |
| `OPER` | `OPER-CHANGE-ME` | `OPER` | `NOHACK` |
| `GUEST` | `GUEST` | none | none, so the minigame may log in as `GUEST` |

Change these passwords before you use a terminal. Anyone who reads this page knows them.

## Tests

```
make -f tests/host/session.mk ROBCO_CORE=/path/to/RobCo-Core
python3 -m pytest tests/tools/test_mkuaf.py
```
