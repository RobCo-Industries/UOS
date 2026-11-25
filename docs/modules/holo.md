# Module holo: data holotapes

The holo module reads data holotapes. A tape is a file `/UOS/HOLO/NAME.HTP` that holds a few text entries under a title. The pure code validates a whole tape in memory, gives access to its title and entries, and runs the reader model: the tape list, the entry list and the request to show one entry in the pager. Nothing here is validated on real hardware.

Files: `src/holo/holofmt.h` (public header), `src/holo/holofmt.c` (format), `src/holo/holoread.c` (reader model), `src/holo/holoscan.c` and `holoscan.h` (the screen's pure rules, prefix `hs_`), `src/holo/holoscr.c` and `holoscr.h` (the screen and the `holo` service), `tools/mkholo.py` (tape builder), `card/UOS/HOLO/` (sample tapes and their sources).

## File layout

A tape is a sealed rimg as defined in RobCo-Core `docs/spec/IMAGE_FORMAT.md`. All numbers are little-endian.

| File offset | Size | Content |
|---|---|---|
| `0x000` | `0x20` | Stub: word 0 is `0xEAFFFFFE`, the ARM instruction `b .`. The other 28 bytes are 0. |
| `0x020` | `0x40` | rimg header |
| `0x060` | `0xC0` | rimg META block: title and icon |
| `0x120` | rest | Payload: the HTXT archive |

The header fields a data tape uses:

| Field | Value |
|---|---|
| `magic` | `"ROBCO78\0"` |
| `hdr_version`, `hdr_size` | 1, 64 |
| `flags` | 1: META present |
| `load_addr` | 0. A data tape never runs and has no slot. |
| `file_size` | the file length |
| `mem_size` | equal to `file_size` |
| `crc32` | CRC of the whole file, as the rimg spec says |
| `abi_version` | `1 << 16`. The minor is not checked. |
| `image_version` | 0 from the tool. Not checked. |
| `subtype` | 2, data |
| `name` | the output file stem, upper case, NUL padded. Not checked. |
| `reserved` | 0 |

The META title holds 1 to 63 printable ASCII characters, then NUL to the end of its 64 bytes. The icon is not checked; the tool leaves it blank.

The rimg spec does not say what the stub of a data tape holds. This module uses a branch to itself, so a mistaken jump to byte 0 does nothing harmful. The validator does not check the stub, but the CRC covers it.

### The HTXT archive

Offsets below are from the start of the payload, file offset `0x120`.

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | magic `"HTXT"` |
| 4 | 2 | version, 1 |
| 6 | 2 | count, 1 to 32 |
| 8 | 40 × count | entry table |
| after the table | rest | text bodies |

Each entry, 40 bytes:

| Offset | Size | Field |
|---|---|---|
| 0 | 32 | name: 1 to 31 printable ASCII characters, then NUL to the end of the field |
| 32 | 4 | offset of the body from the payload start |
| 36 | 4 | length of the body, 0 to 65536 |

A body lies inside the payload and after the table. Two bodies may not share a byte. Empty bodies share nothing. Gaps between bodies and bytes after the last body are allowed. The tool writes bodies in entry order with no gaps.

Bodies are plain ASCII with LF or CR LF line ends. The validator does not check body bytes. At display time `hf_display_char()` shows any byte other than printable ASCII, tab, CR and LF as `.`. This is not an error.

## Limits

| Constant | Value |
|---|---|
| `HF_ENTRIES_MAX` | 32 entries |
| `HF_ENTRY_TEXT_MAX` | 65536 bytes per entry |
| `HF_NAME_MAX` | 31 characters per entry name |
| `HF_TITLE_MAX` | 63 characters per tape title |
| `HF_TAPE_MAX` | 2098728 bytes: `0x120 + 8 + 32 × 40 + 32 × 65536` |
| `HF_TAPES_MAX` | 32 tapes in the reader's list |

The caller reads a whole tape into one buffer before it calls `hf_open()`. To read any valid tape, it needs `HF_TAPE_MAX` bytes from the kernel allocator. A larger file is refused with `HF_E_TOOBIG`.

## Validation

`hf_open()` checks in this order and returns the first failure:

| Code | Name | Check |
|---|---|---|
| `HF_E_ARG` | `arg` | NULL tape or image |
| `HF_E_TOOBIG` | `toobig` | size above `HF_TAPE_MAX` |
| `HF_E_IO` | `io` | fewer than `0x60` bytes, through `rimg_check_header()` |
| `HF_E_MAGIC` | `magic` | rimg step 1, magic `ROBCO78` only |
| `HF_E_VERSION` | `version` | rimg step 2 |
| `HF_E_SIZE` | `size` | rimg step 3, `file_size` equals the bytes read |
| `HF_E_SUBTYPE` | `subtype` | subtype is not 2 |
| `HF_E_BOUNDS` | `bounds` | `load_addr` is not 0 or `mem_size` is not `file_size` |
| `HF_E_ABI` | `abi` | ABI major is not 1 |
| `HF_E_CRC` | `crc` | CRC, through `rimg_crc()` |
| `HF_E_TITLE` | `title` | META title |
| `HF_E_ARCSHORT` | `arcshort` | payload shorter than 8 bytes |
| `HF_E_ARCMAGIC` | `arcmagic` | archive magic |
| `HF_E_ARCVER` | `arcver` | archive version |
| `HF_E_COUNT` | `count` | count not 1 to 32 |
| `HF_E_TABLE` | `table` | table runs past the payload |
| `HF_E_NAME` | `name` | entry name, per entry in order |
| `HF_E_LENGTH` | `length` | entry length over 65536, per entry |
| `HF_E_RANGE` | `range` | body outside the payload or inside the table, per entry |
| `HF_E_OVERLAP` | `overlap` | two bodies share a byte, after all entries |

The reader model adds `HF_E_INDEX`, `HF_E_BUF` and `HF_E_STATE`. `hf_err_name()` gives the short name and `hf_strerror()` one line of text, at most 48 characters.

RobCo-Core's `rimg_validate()` cannot accept a data tape: subtype 2 has no slot, so step 4 always fails. The module therefore runs steps 1 to 3 through `rimg_check_header()`, checks steps 4 and 5 itself with the rules above, and runs step 6 with `rimg_crc()`.

## Public functions

| Function | Does |
|---|---|
| `hf_open(t, image, size)` | Validates a tape in memory and fills `t` |
| `hf_title(t)` | Tape title, NUL-terminated |
| `hf_count(t)` | Number of entries |
| `hf_entry_name(t, i)` | Entry name, NUL-terminated, or NULL |
| `hf_entry_text(t, i, &text, &len)` | Pointer to the body and its length |
| `hf_display_char(c)` | The character to draw for a body byte |
| `hf_err_name(code)`, `hf_strerror(code)` | Error name and text |
| `hf_reader_init(r, tapes, n)` | Starts the reader on the tape list |
| `hf_reader_event(r, ev, &act)` | Applies UP, DOWN, ENTER or BACK |
| `hf_reader_loaded(r, image, size)` | Opens the tape the reader asked for |
| `hf_layout(r, lines, n)` | Fills the 30 lines of a list screen |

The tape and its text point into the caller's buffer. They stay valid while the buffer lives.

## Reader model

The caller scans `/UOS/HOLO`, validates each tape with `hf_open()`, and fills one `hf_tape_info_t` per file: the 8.3 file name, the title, and the error code. A tape with an error shows as damaged with the reason and cannot be opened. `HF_E_UNCHECKED` is not damage: the caller could not read the tape, so its bytes were never checked. Such a tape can be opened; the load reads it again.

| Screen | UP, DOWN | ENTER | BACK |
|---|---|---|---|
| tape list | move, with wrap | `HF_ACT_LOAD` for a good or unchecked tape, `HF_ACT_REFUSED` for a damaged one | `HF_ACT_EXIT` |
| entry list | move, with wrap | `HF_ACT_SHOW` with the entry name and text; goes to reading | `HF_ACT_CLOSE`; back to the tape list |
| reading | nothing | nothing | back to the entry list |

On `HF_ACT_LOAD` the caller reads the file and calls `hf_reader_loaded()`. If the tape fails validation, the model stays on the tape list and returns the code, and the caller marks that tape damaged. On `HF_ACT_SHOW` the pager shows the text and sends BACK when it closes. On `HF_ACT_CLOSE` the caller may free the tape buffer.

Layout of both list screens, 30 lines of at most 78 characters:

| Line | Tape list | Entry list |
|---|---|---|
| 0 | `UOS HOLOTAPE READER` | same |
| 1 | `Holotapes in SYS$HOLO: N` | `Tape: <title>` |
| 2 | 78 `-` | same |
| 3 to 27 | `> NAME.HTP     <title>`, file name padded to 12 | `> 01  <entry name>` |
| 28 | 78 `-` | same |
| 29 | `UP/DOWN move   ENTER open tape   BACK leave reader` | `UP/DOWN move   ENTER read entry   BACK tape list` |

`>` marks the selection; other rows start with two spaces. A damaged tape shows `DAMAGED: <reason>` in place of its title. An unchecked tape shows `NOT CHECKED: select it to try again`, 34 characters after the file name, so the row stays within 78. An empty list shows `  No holotapes found in SYS$HOLO.` on line 3. The window holds 25 rows and scrolls to keep the selection in view.

## The screen

`holo_screen_start(done, ctx)` in `holoscr.h` pushes the reader, loop only. It returns 0, `HOLO_ERR_BUSY` when the reader is open, or `HOLO_ERR_SCREEN`. `done(ctx)` runs once after the reader leaves the stack. The shell's `RUN` verb can register it.

The reader opens over the top screen, at the login or in a session, and closes only itself. `holo open` calls `holo_screen_start(NULL, NULL)`.

| How it closes | What is on top after |
|---|---|
| BACK on the tape list | The screen it was opened over, with its state |
| Logout or idle timeout in a session | `sess_logout()` removes every screen above the bottom one: the login prompt. The tape is freed and `done` runs |
| A win or a loss of the password game | Cannot happen: the game does not play over the reader |
| No card, a damaged tape, no memory | The reader stays open with its message line |

The screen follows the model's actions:

- It lists `SYS$HOLO:*.HTP` with `vfs_list()`, keeps the first 32, then reads one tape per loop pass with `vfs_read_all()` and fills `hf_tape_info_t` from `hf_open()`. Only a file over `HF_TAPE_MAX` is damage from a failed read: `toobig`. Every other read error says nothing about the tape's bytes, so the tape is `unchecked`: no memory, a busy or missing card, a file gone since the listing, a FAT read error that may pass, and no access. `hs_read_err()` makes that choice.
- `HF_ACT_LOAD`: it draws `LOADING NAME.HTP ...`, then reads the file on the next pass and calls `hf_reader_loaded()`. The buffer comes from the heap through `vfs_read_all()`. A validation error or a file over the limit marks the tape damaged. No heap, no card, a vanished file and any other read error show a line and leave the tape as it is. A good load of an unchecked tape marks it good and fills its title.
- Load results and refusals go to the message row and, once, to the serial line. The `LOADING` line is on the screen only.
- `HF_ACT_SHOW`: `con_page_text()` with the entry name as the title. When the pager closes, the screen sends BACK to the model.
- `HF_ACT_CLOSE`: `vfs_free()` of the tape.
- `HF_ACT_EXIT`: the screen pops.

UP and DOWN move, SELECT or RIGHT is ENTER, BACK or LEFT is BACK. `hs_key()` maps the keys: a held UP or DOWN repeats, ENTER and BACK take only the press, so a held key does one step. Rows starting with `>` show in inverse. The screen adds its own lines to the model's layout with `hs_apply()`:

| Case | Line |
|---|---|
| No card | line 3: `No card. Insert a UOS card to read holotapes.` |
| No `HOLO` folder, or no `/UOS` | line 3: `This card has no holotape folder, SYS$HOLO.` |
| Over 32 tapes | line 1: `Holotapes in SYS$HOLO: N, first 32 shown` |
| A message | line 28, in place of the rule: `** <text>` |

Reads use the session's file context and privileges, or no privilege when nobody is logged in: `SYS$HOLO` is readable by every user. See [ARCHITECTURE.md](../ARCHITECTURE.md#holotape-reader) for the load times.

### Markers

| Marker | When |
|---|---|
| `holo op=open`, `holo op=exit` | The reader enters and leaves the stack |
| `holo op=tape i= file= result= bytes= us= rc=` | One per scanned tape. `result` is the `hf_err_name()` word |
| `holo op=scan result=<list\|empty\|nocard\|nofolder\|error> total= kept= damaged= unchecked= rc=` | The scan ended |
| `holo op=load file= result=<ok\|nomem\|nocard\|gone\|damaged\|retry> why= bytes= us= ticks=` | A load ended; `result=refused` for a damaged tape. `retry`: a read error that is not about the tape, with `THIS HOLOTAPE COULD NOT BE READ NOW. TRY AGAIN.` |
| `holo op=show tape= entry= bytes= rc=` | An entry opens in the pager |
| `holo op=close file=` | The open tape's buffer is freed |
| `holo result=<busy\|range\|screen\|gone>` | A `holo` command did nothing. `gone`: the reader the pick was meant for closed before the pick ran, or none is open |

### The command

| Command | Does |
|---|---|
| `holo open` | Posts `holo_screen_start()` |
| `holo list` | One line per tape: `tape i= file= err= title=`, the title with `%XX` for blanks |
| `holo pick <n>` | Moves the selection of the shown list to item `n`, then ENTER. The posted call carries `n` and the reader it was meant for, so two picks in one write each keep their own |
| `holo state` | `holo state= scan= screen= tapes= total= sel= entry= open= msg= pending=`. `pending` counts input events not handled yet through `uos_input_pending()`, posted commands and a running scan or load |
| `holo hog` | Test builds only: takes the heap until no tape fits, a second call gives it back |

## The tool

```
mkholo.py build --title TEXT --out NAME.HTP [--rimg PATH] FOLDER
mkholo.py list [--rimg PATH] NAME.HTP
mkholo.py check [--rimg PATH] NAME.HTP...
```

`build` takes the `.TXT` files of `FOLDER` in name order. Without `ORDER.TXT`, the first line of each file is the entry name and the rest is the body. With `ORDER.TXT`, each line that is not blank and does not start with `#` holds a file name, spaces, and the entry name; the whole file is the body, and every `.TXT` file must be listed once.

The tool finds RobCo-Core's `tools/rimg.py` through `--rimg` or `$ROBCO_CORE/tools/rimg.py`. `rimg.py seal` and `pack` refuse data tapes, because they validate the slot and subtype 2 has none. So `build` loads `rimg.py` as a module, uses its layout constants, title encoding and CRC, writes the header itself, and validates the result before it writes the file. `rimg.py info` reads the result and reports `crc_ok=1`.

`list` prints the title and one line per entry with its length. `check` prints `ok` or the error name and text for each tape, with the same names and order as `hf_open()`.

| Exit | Meaning |
|---|---|
| 0 | success |
| 1 | bad input or an invalid tape: too many entries, an entry over 65536 bytes, a byte that is not printable ASCII, tab, CR or LF, a title or name too long or empty, a bad `ORDER.TXT` |
| 2 | a file cannot be read or written, or the folder is missing |
| 3 | `rimg.py` not found |
| 64 | usage error |

## Sample tapes

| File | Title | Entries |
|---|---|---|
| `TECHLOG.HTP` | Alder Creek technician's log | 4 |
| `PROCS.HTP` | Alder Creek operating procedures | 4, with `ORDER.TXT` |
| `STORY.HTP` | The rake with the longer handle | 4 |

Sources are in `card/UOS/HOLO/SRC/<TAPE>/`. The text is original lore about a small water pump station. A test rebuilds each tape from its source and compares the bytes.

## Tests

`make test-host` runs `test_holo` and `test_holo_screen`. `tests/test_holo.py` runs the reader in QEMU. `make test-tools` builds `build/host/holocheck` from `tests/host/holocheck.c` and runs `tests/tools/test_mkholo.py` against it. See [TESTING.md](../TESTING.md).
