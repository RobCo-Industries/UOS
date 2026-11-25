# Hack: the password minigame

`src/hack/hack.c` is the pure model of the password hacking minigame. `src/hack/hackui.c` holds the screen's pure rules, prefix `hg_`, and `src/hack/hackscr.c` the screen and the `hack` service. Public header: `src/hack/hack.h`, prefix `hk_`. It owns the word lists, candidate selection, the board, the cursor, the game state and the lockout timer. Drawing code reads the board and the history and shows them. Nothing here is validated on real hardware.

## Rules of the game

The board shows a memory dump with candidate words hidden among junk characters. One word is the password. The player moves a cursor and selects a word to guess it.

- A correct guess wins.
- A wrong guess uses one attempt and shows the likeness: the number of positions where the guess and the password hold the same letter.
- At zero attempts the terminal locks for `lockout_s` seconds.
- A bracket pair is a matched opening and closing bracket on one row with no letter between them: `()`, `[]`, `{}` or `<>`. Selecting its opening bracket uses it. With chance 1 in 5 it resets the attempts to the configured count. Otherwise it removes one dud: a wrong word chosen at random becomes dots and can no longer be selected. When no dud is left, the bracket always resets the attempts. The password is never removed.
- Each pair works once. A used pair stays on the board but is no longer highlighted, and selecting it does nothing.
- Selecting a junk character, a dot or a closing bracket does nothing.
- Guessing the same wrong word twice uses an attempt each time.

## Difficulty table

| Difficulty | Word length | Words | Name in `UOS.CFG` |
|---|---|---|---|
| novice | 4 or 5 | 8 | `novice` |
| advanced | 6 to 8 | 12 | `advanced` |
| expert | 9 or 10 | 14 | `expert` |
| master | 11 or 12 | 16 | `master` |

`hk_pick_length` chooses the length for one game with the generator. The caller then reads `/UOS/WORDS/Wnn.TXT` for that length. Names parse case-insensitively.

Game settings, from `UOS.CFG [hack]`: `attempts` 1 to 9 (default 4), `lockout_s` 0 to 3600 (default 60). The caller also passes the tick rate, 1 to 1000000 ticks per second. Parsing the INI is the caller's job.

## Generator

xorshift32 with shifts 13, 17, 5. Seed 0 becomes `0x9E3779B9`. `hk_rng_below(n)` is `(next * n) >> 32` on 64 bits: no division, a small bias. `hk_game_init` copies the caller's generator, so the caller's state does not move.

## Word list format

- One word per line, `A` to `Z` only, all the same length.
- Line ends LF or CR LF. Blank lines are skipped. The last line may lack a line end.
- `hk_list_parse` keeps pointers into the caller's buffer, so the buffer must live until the board is built. The board copies the letters.
- Errors, with `err_line` set to the 1-based line: `HK_ERR_CHAR` for any other character, a space or a lone CR included; `HK_ERR_LENGTH` for a word of another length; `HK_ERR_DUP` for a repeated word; `HK_ERR_TOO_MANY` past 512 words. A length outside 4 to 12 is `HK_ERR_ARG`.
- `hk_pick` returns `HK_ERR_TOO_FEW` when the list holds fewer words than the difficulty needs, and `HK_ERR_LENGTH` when the list's length is outside the difficulty's range.

## Selection rule

With `n` the word count, `k = n - 1` the other words and `m = count - 1` the pool without the password:

1. Choose the password uniformly from the list.
2. Shuffle the pool, then sort it by likeness to the password, highest first. The sort is stable, so ties land in random order.
3. Cut the sorted pool into three bands of `b = m / 3` words: near `[0, b)`, middle `[b, 2b)` and far `[2b, m)`.
4. Take `ceil(k / 3)` random words from near, `(k + 1) / 3` from middle and `floor(k / 3)` from far.
5. Shuffle the set with the password and record the password's index.

When `b < ceil(k / 3)` the bands are too small. The rule then takes `k` distinct random words from the whole pool and sets `spread_ok = 0`. A valid list with at least `n` words never fails. Every card list has at least 64 words, so the band rule always holds there. No two words can have full likeness, because duplicates are rejected.

## Board layout

- Two columns of 16 rows of 12 characters: 384 cells in `cell[]`. Cell index is `col * 192 + row * 12 + x`. A column reads as one run of 192 characters, so a word may wrap from one row to the next inside a column.
- Addresses: the start is `0x1000 + rng_below(0xEE80)`, so the last cell stays at or below `0xFFFF` and every address has four hex digits. Row `r` of column `c` has address `start + (c * 16 + r) * 12`. `hk_format_address` writes `0xF964` style text and needs 7 bytes.
- Junk is drawn from ``!"#$%&'()*+,-./:;<=>?@[\]^_{|}``.
- Words: the left column takes `ceil(n / 2)` words, the right column the rest, in the order of the candidate set. In a column, each pair of neighbour words has at least one junk character between them. The spare junk is spread at random over the gaps before, between and after the words. `test_hack.c` checks the one-junk rule with all gaps at zero, since random boards almost never draw a zero gap. No word crosses from the left column to the right.
- Bracket pairs: an opening bracket pairs with the first closing bracket of its kind to its right on the same row, if no letter lies between. Other junk, other brackets included, may lie between. Two openers may share one closer. Pairs are found once when the board is built, so dots from a removed dud never create new pairs.
- At least 4 usable pairs exist on every board. When the random junk holds fewer, the builder writes an opener and its closer into two neighbour cells on one row that hold junk but no bracket. That never breaks an existing pair. It tries 64 random spots, then scans from cell 0.

## Cursor

The cursor sits on one cell and starts at cell 0.

- Left and right move one cell along the row. Right from the last cell of a left-column row goes to the first cell of the same row in the right column. Left does the reverse. At the outer edges the cursor stays.
- Up and down move one row in the same column. At the top and bottom rows the cursor stays.

`hk_highlight` returns what to highlight:

- `HK_HL_WORD`: the cursor is on a live word. The highlight is the whole word, from its first cell, and may span two rows.
- `HK_HL_PAIR`: the cursor is on the opening bracket of an unused pair. The highlight runs from the opener to its closer.
- `HK_HL_CHAR`: anything else, one cell.

## History lines

The side column keeps the last 16 lines, newest at the bottom. `hk_history_line(g, 0)` is the oldest kept line. Each line is at most 12 characters.

| Event | Lines |
|---|---|
| Correct guess | the word, `Exact match.` |
| Wrong guess | the word, `Entry denied.`, `Likeness=N` |
| Bracket, dud removed | the bracket text from opener to closer, `Dud removed.` |
| Bracket, tries reset | the bracket text, `Tries reset.` |

## Game states and the result

`hk_select` returns `HK_EV_NONE`, `HK_EV_DENIED`, `HK_EV_WIN`, `HK_EV_LOCKOUT`, `HK_EV_DUD` or `HK_EV_RESET`. `hk_result` returns the state:

- `HK_PLAYING`.
- `HK_WON`: log in the target user.
- `HK_LOCKED`: `*secs` gets the whole seconds left, rounded up, and 0 once the lockout is over. Elapsed time is `now - lock_start` on 32 bits, so one counter wrap is handled. Lock ticks are `lockout_s * ticks_per_s`, at most 3.6e9, which fits 32 bits. The caller must poll more often than once per 2^32 ticks.
- `HK_CANCELLED`: after `hk_cancel`, which only acts while playing.

Once the game leaves `HK_PLAYING`, `hk_select` does nothing.

## Public functions

| Function | What it does |
|---|---|
| `hk_strerror` | one line of text per error code |
| `hk_rng_seed`, `hk_rng_next`, `hk_rng_below` | the generator |
| `hk_difficulty_parse`, `hk_difficulty_name`, `hk_difficulty_info` | the difficulty table |
| `hk_pick_length` | the word length for one game |
| `hk_likeness` | matching letter positions |
| `hk_list_parse` | parse and validate one list |
| `hk_pick` | choose the candidate set |
| `hk_board_build` | build the board |
| `hk_column_free`, `hk_column_place` | one column's word layout: the spare junk, and each word's offset from the gaps |
| `hk_board_address`, `hk_format_address` | row addresses and their text |
| `hk_board_word_at`, `hk_board_pair_count` | board queries |
| `hk_game_init` | start a game: validates settings, builds the board |
| `hk_move`, `hk_highlight`, `hk_select` | the cursor and SELECT |
| `hk_cancel`, `hk_result` | leave the game, read the outcome |
| `hk_history_count`, `hk_history_line` | the side column |

Memory: `hk_list_t` is about 3 KiB with 32-bit pointers, `hk_game_t` about 2 KiB. The caller owns both. No function uses more than a small stack frame.

## The screen

`hack_screen_start(done, ctx)` in `hackscr.h` pushes the game, loop only. It returns 0, `HACK_ERR_BUSY` or `HACK_ERR_SCREEN`. `done(how, ctx)` runs once when the screen leaves: `HACK_DONE_LEFT`, `HACK_DONE_WON` or `HACK_DONE_LOST`. At the login, F2 on an empty user name line starts it. The shell's `RUN` verb can register it.

The game plays only when nobody is logged in and the login's user name prompt is the top screen at the call: `sess_prompt_on_top()`. The pure rule is `lg_hack_where()`. Any other start pushes the screen with its refusal, and BACK pops that screen alone. So a start from a session shows `busy`, a start over the holotape reader shows `covered`, and the screen under it comes back as it was. A win or a loss cannot happen from a session, since the game never plays there.

| How it closes | Started from the login prompt | Started over another screen |
|---|---|---|
| BACK while playing, or after a refusal | `done(LEFT)`; F2 and `hack start` restart the prompt with `sess_resume()` | `done(LEFT)`; the screen under it is the top |
| A win | `sess_hack_begin()`: the stack holds the welcome only, then the next screen | Cannot happen |
| A loss | `sess_hack_lock()`: the stack holds the lockout screen only | Cannot happen |
| The win or loss is refused after the pause | Pops itself, `done(LEFT)`, the prompt restarts | Cannot happen |
| Logout or idle timeout | Cannot happen: nobody is logged in | `sess_logout()` removes every screen above the bottom one, the login comes back |

The call order on start: `sess_hack_check()`, then `hg_config()` from `[hack]`, a seed from `rng_u32()`, `hk_pick_length()`, `vfs_sys_read_all("/UOS/WORDS/Wnn.TXT")`, `hk_list_parse()`, `hk_pick()`, `hk_game_init()` with 100 ticks per second, then the list's buffer is freed. `hk_list_t` and `hk_game_t` are static. Both are cleared when the screen leaves.

Layout, 80 columns:

| Row | Text |
|---|---|
| 0 | `UOS PASSWORD RECOVERY. DIAGNOSTIC MEMORY VIEW.` |
| 1 | `TARGET ACCOUNT: <user>` |
| 3 | `ATTEMPTS LEFT: N` and one `#` per attempt |
| 5 to 20 | Board row `r`: address, 12 cells from column 7; address, 12 cells from column 28; the history line from column 43 |
| 22 | `> ` and the highlighted text |
| 23 | The message row |
| 24 | The keys |

`hg_cell_pos(i)` gives cell `i`'s row and column with the rule `col*192 + row*12 + x`, so a highlight that wraps to the next row of its column draws on both rows. The highlight shows in inverse. History lines sit at the bottom of the board rows, the newest on row 20.

Keys: the four directions move, press or repeat; SELECT guesses, press only; BACK leaves, press only. A held SELECT or BACK acts once. Serial arrow keys and GPIO buttons give the same keys. There is no on-screen keyboard here.

After a win or a loss the result shows for 1 s and keys do nothing. When another screen covers the game at the end of that second, the game waits until it is the top again. Then a win calls `sess_hack_begin()` and a loss `sess_hack_lock()`: see [session](session.md#the-password-game). A win clears the login's failure count, as a right password does. BACK leaves the count as it was. Each start deals a new board with the full `attempts`: see [lockout events](session.md#lockout-events).

The board is drawn on the screen only. The PL011 gets the markers, the refusal and result lines, never the board: the only words it shows are the guessed ones. Each refusal or start error goes out once: `start_game()` only fills the message, and the screen's enter prints it.

| State a person can meet | What happens |
|---|---|
| Word list missing | `WORD LIST /UOS/WORDS/Wnn.TXT IS MISSING.`, BACK leaves |
| Word list damaged or too short | `WORD LIST /UOS/WORDS/Wnn.TXT IS DAMAGED.`, BACK leaves |
| No card | `NO UOS CARD: THE PASSWORD GAME HAS NO WORD LIST.` |
| Game off, target missing, disabled or `NOHACK`, locked, a session running | The session's reason line, BACK leaves |
| Another screen open over the login prompt | `CLOSE THE OPEN PROGRAM: THE GAME STARTS FROM THE LOGIN PROMPT.`, BACK leaves |
| Card removed during the game | The game goes on: the board is in memory and the game reads no file after its start |

### Markers

None names the password before the game ends.

| Marker | When |
|---|---|
| `hack op=rng result=<hw\|fallback> rc=` | At start of the service: `rng_init()`'s result |
| `hack op=start result=<ok\|refused\|nocard\|nolist\|badlist\|nomem\|config> why= len= words= attempts= difficulty= lockout_s= seed=<hw\|fixed> line=` | A game starts or cannot |
| `hack op=guess word= result=<denied\|win\|lockout> likeness= attempts=` | A guess. The screen shows the same word and likeness |
| `hack op=bracket result=<dud\|reset> attempts=` | A bracket pair was used |
| `hack op=end result=<won\|lost\|left>` | The game ends |
| `hack result=busy` | `hack start` while the game is open |
| `hack result=gone` | A `hack move`, `select` or `back` reached the loop after the game it was meant for ended, or with no game open |
| `hack result=covered` | A `hack move`, `select` or `back` found another screen over the game, such as the holotape reader. Nothing changes |
| `hack op=wait why=covered` | The result pause ended with another screen over the game. The win or loss applies once the game is the top again |

### The command

| Command | Does |
|---|---|
| `hack start` | Posts the start. Over the login prompt it acts as F2. Over any other screen it acts as `hack_screen_start(NULL, NULL)`: the refusal shows, `covered` or `busy`, and BACK gives that screen back |
| `hack move <left\|right\|up\|down> [n]` | Posts `n` moves, 1 to 64 |
| `hack select`, `hack back` | Posts SELECT or BACK. Each posted call carries its own arguments and the game it was meant for. A covered game refuses with `hack result=covered` |
| `hack state` | `hack state= cursor= hl= hlstart= hllen= attempts= result= len= words= pairs= pending=`, where `pending` is `uos_input_pending()` plus posted commands, then `row r= text=` for the 16 board rows and `hist i= text=` per history line, with `%XX` for blanks, `%` and `"` |
| `hack seed <n>` | Test builds only: the next games use seed `n`. `test_hack.py` checks with `nm` that the shipped image has none of it |

## The tool

`tools/mkwords.py`, Python 3, standard library only:

- `check [DIR]`: every list `W04.TXT` to `W12.TXT` exists, holds only A to Z words of its length, is sorted, has no duplicates, holds 64 to 512 words and has no word from the deny list in the tool. Exit 1 on any problem.
- `sort [DIR]`: rewrites each list sorted, without duplicates, with LF line ends.
- `sample N [--length L] [--seed S] [DIR]`: prints `N` distinct random words of one list. It uses Python's `random` and does not mirror the C selection rule.

`DIR` defaults to `card/UOS/WORDS`. The lists there are original common English words.

## Tests

`make test-host` runs `tests/host/test_hack.c` from the repository root, so it reads `card/UOS/WORDS`, and `test_hack_ui.c`. `board_one_junk_between_words` builds columns with every random gap at zero, so a missing separator between two words fails it. `tests/test_hack.py` plays the game in QEMU. `make test-tools` runs `tests/tools/test_mkwords.py`. See [TESTING.md](../TESTING.md).
