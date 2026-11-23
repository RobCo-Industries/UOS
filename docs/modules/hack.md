# Hack: the password minigame

`src/hack/hack.c` is the pure model of the password hacking minigame. Public header: `src/hack/hack.h`, prefix `hk_`. It owns the word lists, candidate selection, the board, the cursor, the game state and the lockout timer. Drawing code reads the board and the history and shows them. Nothing here is validated on real hardware.

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
- Words: the left column takes `ceil(n / 2)` words, the right column the rest, in the order of the candidate set. In a column, each pair of neighbour words has at least one junk character between them. The spare junk is spread at random over the gaps before, between and after the words. No word crosses from the left column to the right.
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
| `hk_board_address`, `hk_format_address` | row addresses and their text |
| `hk_board_word_at`, `hk_board_pair_count` | board queries |
| `hk_game_init` | start a game: validates settings, builds the board |
| `hk_move`, `hk_highlight`, `hk_select` | the cursor and SELECT |
| `hk_cancel`, `hk_result` | leave the game, read the outcome |
| `hk_history_count`, `hk_history_line` | the side column |

Memory: `hk_list_t` is about 3 KiB with 32-bit pointers, `hk_game_t` about 2 KiB. The caller owns both. No function uses more than a small stack frame.

## The tool

`tools/mkwords.py`, Python 3, standard library only:

- `check [DIR]`: every list `W04.TXT` to `W12.TXT` exists, holds only A to Z words of its length, is sorted, has no duplicates, holds 64 to 512 words and has no word from the deny list in the tool. Exit 1 on any problem.
- `sort [DIR]`: rewrites each list sorted, without duplicates, with LF line ends.
- `sample N [--length L] [--seed S] [DIR]`: prints `N` distinct random words of one list. It uses Python's `random` and does not mirror the C selection rule.

`DIR` defaults to `card/UOS/WORDS`. The lists there are original common English words.

## Tests

```
make -f tests/host/hack.mk ROBCO_CORE=/path/to/RobCo-Core
```

Run from the repository root. It builds and runs `tests/host/test_hack.c` with sanitizers, compiles `src/hack/hack.c` with `arm-none-eabi-gcc` and the target flags, and runs `tests/tools/test_mkwords.py`. The tool test also runs under pytest.
