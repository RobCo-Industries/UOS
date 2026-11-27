# On-screen keyboard

The on-screen keyboard lets a person type text with arrow, select and back buttons only. `src/osk/osk.c` holds the pure model. A thin drawing file paints what the model describes. Nothing here is validated on real hardware.

## Layout

The keyboard covers the bottom 5 rows of the console. Rows 0 to 3 hold 13 character keys each, 6 cells wide. Row 4 holds 6 function keys, 13 cells wide. The grid starts at column 1 and ends at column 78. Each label sits in the middle of its cell; an odd leftover space goes to the right. The space key shows `SPC`.

Layer 0, letters and the command symbols:

```
   A     B     C     D     E     F     G     H     I     J     K     L     M
   N     O     P     Q     R     S     T     U     V     W     X     Y     Z
   0     1     2     3     4     5     6     7     8     9     .     /     :
   $     _     -     *    SPC    [     ]     ;     %     =     (     )     ,
     SHIFT        BKSP         LEFT         RIGHT        ENTER        CLOSE
```

Layer 1, lower case and the other symbols:

```
   a     b     c     d     e     f     g     h     i     j     k     l     m
   n     o     p     q     r     s     t     u     v     w     x     y     z
   !     "     #     &     '     +     <     >     ?     @     \     ^     `
   {     |     }     ~     .     ,     :     ;     /     _     -     =    SPC
     SHIFT        BKSP         LEFT         RIGHT        ENTER        CLOSE
```

Every printable ASCII character from space to `~` is on some key, so any password can be typed. Layer 1 repeats a few common symbols to fill its last row. Of the design's command symbols, only `"` is on layer 1.

## Keys and rules

| Input | Effect |
|---|---|
| LEFT, RIGHT | Move along the row. Wrap at both ends. |
| UP, DOWN | Move between rows. Row 0 and the function row wrap to each other. |
| SELECT on a character | Emits one `INPUT_EV_CHAR` with the character as `code`. |
| SELECT on BKSP | Emits `INPUT_KEY_BACK` press and release. |
| SELECT on LEFT, RIGHT | Emits `INPUT_KEY_LEFT` or `INPUT_KEY_RIGHT` press and release. |
| SELECT on ENTER | Emits `INPUT_KEY_SELECT` press and release, then closes. |
| SELECT on SHIFT | Goes to the next layer. The highlight stays where it is. |
| SELECT on CLOSE | Closes. |
| BACK | Closes. |

These are the codes the library's serial parser gives for the same keys. CR gives SELECT and DEL gives BACK, so the line editor reads both sources the same way.

Movement between a character row and the function row uses cell centres. Going to the function row picks the function key under the centre of the character key: columns 0 to 12 go to keys 0, 0, 1, 1, 2, 2, 3, 3, 3, 4, 4, 5, 5. Going back picks the character key under the centre of the function key: keys 0 to 5 go to columns 1, 3, 5, 7, 9, 11. Down then up from a function key returns to the same key. Both layers have a key at every position, so SHIFT never moves the highlight.

Repeat events: a held arrow keeps moving. A held SELECT repeats only on BKSP, LEFT and RIGHT. It does nothing on characters, ENTER, SHIFT and CLOSE. A held BACK does nothing. Release events are used and ignored.

## Public functions

| Function | What it does |
|---|---|
| `osk_init` | Closed, layer 0, highlight on `A`, empty queue, password off. |
| `osk_open` | Opens at layer 0 on `A`, sets the password flag, clears the queue. |
| `osk_close` | Closes. Queued events stay. |
| `osk_is_open`, `osk_password`, `osk_layer`, `osk_highlight` | Read the state. |
| `osk_handle` | Feeds one event. Returns `OSK_IGNORED`, `OSK_CONSUMED` or an error. |
| `osk_pop`, `osk_pending` | Drain the output queue. |
| `osk_row_text` | Fills a caller buffer with one row of the current layer: 80 characters and a NUL. |
| `osk_key_cell` | Gives one key's column, row, width, character or function and whether it is highlighted. |
| `osk_screen_rows` | Gives the screen rows the keyboard covers: the bottom 5 when open, none when closed. |
| `osk_layout_char` | Gives the character at a layer, row and column. |
| `osk_strerror` | One line of text for a result or error code. |

`osk_handle` ignores events when the keyboard is closed. When open it ignores `INPUT_EV_CHAR`, `INPUT_EV_DIAL` and keys other than the arrows, SELECT and BACK, so serial typing goes on to the line editor.

The password flag only travels with the model. The line editor masks the line.

## Limits and errors

| Name | Value | Meaning |
|---|---|---|
| `OSK_OUT_MAX` | 16 | Events the output queue holds. |
| `OSK_ROWS` | 5 | Screen rows covered. |
| `OSK_KEYS` | 58 | Keys per layer. |
| `OSK_LAYERS` | 2 | Layers. |
| `OSK_ROW_BUF` | 81 | Bytes `osk_row_text` needs. |
| `OSK_SRC` | 3 | `source` of emitted events. |
| `OSK_ERR_FULL` | -1 | The queue lacks room. Nothing is emitted and the state does not change. A key needs 2 free slots. |
| `OSK_ERR_ARG` | -2 | Bad argument. |
| `OSK_ERR_SPACE` | -3 | Buffer too small. Nothing is written. |
| `OSK_ERR_SCREEN` | -4 | The screen has fewer than 5 rows. |

## What the caller does

The caller opens the keyboard when a line is being read and a GPIO button event arrives, or on a dedicated key. One button does not open it: at `USERNAME:`, on an empty line, BACK from a button starts the password game, as F2 does. `cv_alt_key()` in `src/console/conview.c` decides that case. A BACK from the serial line never starts the game. It passes each input event to `osk_handle` first. On `OSK_IGNORED` it gives the event to the line editor. After each call it drains `osk_pop` into the line editor. It must not feed emitted events back into `osk_handle`. They carry `source` 3 so the caller can tell them apart. The caller draws `osk_row_text` rows on the rows `osk_screen_rows` gives and paints the highlighted cell in inverse.
