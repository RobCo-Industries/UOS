# The terminal screen (tui)

The `tui` module is the menu-driven terminal screen. It shows a header, a menu of entries and a status area. A person moves through the entries with the arrow keys. The module also holds the pager that many screens use for long text.

All code is pure. It reads no files and touches no hardware. The caller loads every file the model asks for. Nothing is validated on real hardware.

Files: `src/tui/tui.h` (public header), `src/tui/tuicfg.c` (menu parser), `src/tui/tui.c` (model and layout), `src/tui/pager.c` (pager, prefix `pg_`).

## TERMINAL.CFG

Each user's home holds `TERMINAL.CFG`, an INI file. Sub-menus are other `.CFG` files in the same home.

```
[terminal]
title = Site office terminal
header = HEADER.TXT

[entry 1]
label = Read the duty log
type = text
target = DUTY.TXT
privs = 0
```

`[terminal]` keys, both optional:

| Key | Value |
|---|---|
| `title` | 0 to 60 printable characters. Without a title the screen shows the file name. |
| `header` | An 8.3 file name in the home. Its first 8 lines show above the menu. Empty means none. |

`[entry N]` sections, N from 1 to 999 without a leading zero. Entries show in order of N. Gaps are fine.

| Key | Value |
|---|---|
| `label` | Required. 1 to 60 printable characters. |
| `type` | Required. `text`, `menu`, `command`, `device`, `holotape` or `logout`, lower case. |
| `target` | Depends on the type, see below. |
| `privs` | Optional. A decimal or `0x` hex 32-bit mask. The entry shows only when the user holds every bit in it. A user with SYSTEM, bit 0, sees every entry. |

Targets by type:

| Type | Target |
|---|---|
| `text` | A plain 8.3 file name: 1 to 8 of `A-Z 0-9 $ _ -`, an optional `.` and 0 to 3 more. No device, no directory. Stored upper case. |
| `menu` | A plain 8.3 file name with the type `.CFG`. |
| `command` | A command line, 1 to 127 printable characters, kept as written. |
| `device` | A device name, 1 to 15 of `A-Z 0-9 _`, then an action word, 1 to 15 letters. Stored as `NAME WORD` in upper case. |
| `holotape` | Empty for the tape list, or an 8.3 tape name. |
| `logout` | Must be empty. |

A plain file name means a menu cannot reach outside the user's home.

Rules:
- At most 16 valid entries per file, hidden ones included. The 17th valid entry and later ones are skipped with `TUI_E_TOO_MANY`.
- A second `[entry N]` with the same N is skipped: the first wins.
- A key given twice in a section: the first wins and the error is counted.
- An unknown key in an entry skips the entry. An unknown key in `[terminal]` is ignored.
- An unknown section, and keys before any section, are ignored.
- Every error is counted in `errors`. The first 8 are kept with their code, line and entry number.
- A text that is not valid INI is refused as a whole with `TUI_E_SYNTAX` and its line.
- The INI reader's own limits apply: 255 bytes per line, 127 per value.

## Error codes

| Code | Value | Text |
|---|---|---|
| `TUI_OK` | 0 | ok |
| `TUI_E_ARG` | -1 | bad argument |
| `TUI_E_SYNTAX` | -2 | menu file does not parse |
| `TUI_E_SECTION` | -3 | unknown section |
| `TUI_E_KEY` | -4 | unknown key |
| `TUI_E_DUP_KEY` | -5 | key given twice |
| `TUI_E_ENTRY_NUM` | -6 | bad entry number |
| `TUI_E_DUP_ENTRY` | -7 | entry number given twice |
| `TUI_E_TOO_MANY` | -8 | more than 16 entries |
| `TUI_E_LABEL` | -9 | bad or missing label |
| `TUI_E_TYPE` | -10 | bad or missing type |
| `TUI_E_TARGET` | -11 | bad target for the type |
| `TUI_E_PRIVS` | -12 | bad privilege mask |
| `TUI_E_TITLE` | -13 | bad title |
| `TUI_E_HEADER` | -14 | bad header file name |
| `TUI_E_DEPTH` | -15 | menus nested too deep |
| `TUI_E_STATE` | -16 | not possible now |
| `TUI_E_SPACE` | -17 | buffer too small |

`tui_strerror()` returns the text.

## Public functions

| Function | What it does |
|---|---|
| `tui_cfg_parse` | Parses `TERMINAL.CFG` text for a user's privileges into a `tui_menu_t`. |
| `tui_is_83` | Checks a plain 8.3 file name. |
| `tui_type_name` | Name of an entry type. |
| `tui_strerror` | Text of an error code. |
| `tui_init` | Starts the model for a user with a root menu file. Returns `TUI_ACT_LOAD_MENU`. |
| `tui_key` | One key: an `INPUT_KEY_*` code or `TUI_CHAR(c)`. Returns an action. |
| `tui_event` | One `input_event_t`. Key presses and repeats, and characters. |
| `tui_give_menu` | Answers `TUI_ACT_LOAD_MENU` with the file text. |
| `tui_give_header` | Answers `TUI_ACT_LOAD_HEADER` with the file text. |
| `tui_give_error` | Answers any action with a failure line. |
| `tui_done` | Ends a text, command, device or holotape action, with an optional result line. |
| `tui_rows` | Rows of entries in the window. |
| `tui_layout` | Fills a caller buffer with the screen lines. |

## The model

The model keeps a stack of up to 8 menus, the selection and the scroll window of each. It never reads a file. It returns an action and the caller answers:

| Action | Fields | The caller |
|---|---|---|
| `TUI_ACT_NONE` | | redraws |
| `TUI_ACT_LOAD_MENU` | `name` | loads the file, calls `tui_give_menu` or `tui_give_error` |
| `TUI_ACT_LOAD_HEADER` | `name` | loads the file, calls `tui_give_header` or `tui_give_error` |
| `TUI_ACT_SHOW_TEXT` | `name` | loads the file, shows it with the pager, calls `tui_done` or `tui_give_error` |
| `TUI_ACT_COMMAND` | `text` | runs the command line, calls `tui_done` with one result line |
| `TUI_ACT_DEVICE` | `name`, `word` | acts on the device, calls `tui_done` with one result line |
| `TUI_ACT_HOLOTAPE` | `name`, empty for the list | runs the holotape reader, calls `tui_done` |
| `TUI_ACT_LOGOUT` | | logs the user out |

While an action is out, keys are ignored. After it the menu comes back with the selection kept. A result or error line shows until the next key. That key also does its normal job.

A sub-menu that fails to load leaves the parent menu shown with the error line. A menu file that does not parse shows `Bad menu file NAME (line L)`. A file with skipped errors loads and shows `NAME: E errors, first: TEXT (line L)`.

## Keys

In a menu:

| Key | Does |
|---|---|
| UP, DOWN | Move the selection, with wrap. |
| 1 to 9 | Select entry 1 to 9. A number past the last entry is ignored. |
| ENTER, SELECT, dial push | Choose the selected entry. |
| BACK, LEFT | Go up a level. At the top level, ask to log out. |

Choosing a `menu` entry at depth 8 shows `Menus nest 8 deep at most` and loads nothing.

At the log out question: ENTER or SELECT logs out, BACK or LEFT returns to the menu. Other keys are ignored.

## Layout

`tui_layout()` fills an array of `tui_line_t`: row, column, attribute (`TUI_A_NORMAL`, `TUI_A_DIM`, `TUI_A_INVERSE`) and text. Every line starts at column 1 and holds at most 78 characters. Blank rows are left out. `TUI_LINES_MAX` (30) lines always suffice. A smaller buffer gives `TUI_E_SPACE`.

| Row | Content | Attribute |
|---|---|---|
| 0 | the system name, `UOS` when none is given | normal |
| 1 | the menu title, or its file name | normal |
| 2 | a rule of 78 `-` | dim |
| 3 to 10 | up to 8 header lines | normal |
| after a blank row, to 26 | the entries: `> Label` for the selection, `  Label` for the others; `(no entries)` when none | selection inverse, others normal |
| 27 | the result, error or log out question | normal |
| 28 | the breadcrumb, titles joined by ` / `; a long trail keeps its end after `...` | dim |
| 29 | the key hint, at most 78 characters | dim |

The entry window has 23 rows minus the header lines: 15 rows with 8 header lines. The window scrolls to keep the selection visible.

Header lines are cut at 78 characters. Tabs become spaces to the next multiple of 8. Other control bytes show as `?`.

Footer hints:

| State | Hint |
|---|---|
| menu, top level | `UP/DOWN: move  1-9: pick  ENTER: choose  BACK: log out` |
| menu, sub-menu | `UP/DOWN: move  1-9: pick  ENTER: choose  BACK: up a level` |
| no entries | `BACK: log out` or `BACK: up a level` |
| log out question | `ENTER: log out  BACK: stay` |
| action out | `Please wait` |
| logged out | `Logged out` |

## The pager

`pg_init()` takes a text buffer the caller keeps alive, a width from 8 to 80 and a height from 1 to 30.

- Lines end at LF, CR LF or a lone CR. A final line end adds no empty line. An empty text has 0 lines.
- Tabs move to the next multiple of 8.
- A line longer than the width breaks after the last blank before the width. Blanks at a break are dropped. A word longer than the width is split. Leading blanks are not a break point.
- Control bytes other than tab show as `?`.

The index is one offset per 64 wrapped lines, in a `uint32_t` buffer the caller passes. Each wrapped line uses at least one byte of text, so `PG_MARKS_FOR(len)`, `len / 64 + 2` entries, always suffices. A 64 KiB text needs at most 1026 entries, 4104 bytes. A smaller buffer gives `PG_E_SPACE`. Finding a line walks at most 63 lines from its mark.

| Function | What it does |
|---|---|
| `pg_init` | Wraps the text and builds the index. |
| `pg_lines`, `pg_pages`, `pg_max_top` | Line count, page count (0 for no lines), last top line. |
| `pg_line_down`, `pg_line_up`, `pg_page_down`, `pg_page_up`, `pg_home`, `pg_end` | Move, clamped at both ends. |
| `pg_visible` | First line and count to draw. |
| `pg_line` | Copies one wrapped line, tabs expanded. The buffer needs width + 1 bytes. |
| `pg_status` | `line A to B of N`, or `line 0 to 0 of 0`. |
| `pg_key` | Applies a key. Returns `PG_EXIT` when the person leaves. |

Pager keys:

| Key | Does |
|---|---|
| UP, `k` | One line up |
| DOWN, `j` | One line down |
| LEFT, F1, `b` | One page up |
| RIGHT, SELECT, F2, space | One page down |
| F3, `g` | First page |
| F4, `G` | Last page |
| BACK, `q` | Leave |

Errors: `PG_E_ARG` (-1), `PG_E_SPACE` (-2), `PG_E_RANGE` (-3, no such line).

## Example content

`card/UOS/HOME/ADMIN`, `OPER` and `GUEST` each hold a `TERMINAL.CFG` with sub-menus, text files and device entries for `DOOR1`, `LIGHT1`, `LOCK1` and `SENSOR1`.

## Tests

`make test-host` runs `test_tui`, `test_tui_cfg` and `test_tui_pager` from the repository root, so the content test reads `card/UOS/HOME`. See [TESTING.md](../TESTING.md).
