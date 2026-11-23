"""The console service: reading a line with its edit keys, history, cancel, a
masked read, the on-screen keyboard, the pager over each test text, and the
errors of its maintenance commands."""

import re

from uos import TIMEOUT, alone, answer, check_golden, dump, dump_until, key, uos_card, \
    wait_marker, wait_ready

# The keyboard covers the bottom 5 rows of the 30-row console, src/osk/osk.h.
OSK_FIRST = 25
OSK_ROWS = 5
ROWS = 30
# The pager body: rows 1 to 28 at 78 columns, src/console/pager.c.
BODY = 28
WIDTH = 78
# The generated text of `page big`: 64 lines of 1023 letters.
BIG_LINES = 64 * -(-1023 // WIDTH)
# The fixed long text of `page long`: 120 lines, every tenth one wraps once.
LONG_LINES = 120 + 12

READING = "line state=reading"


def rows_of(vm):
    _, rows = dump(vm)
    assert len(rows) == ROWS, rows
    return [r.rstrip() for r in rows]


def start_read(vm, words, n):
    """Starts the n-th read of this boot and waits until it is on screen."""
    answer(vm, "readline " + words)
    wait_marker(vm, READING, n)


def line_row(vm):
    fields = dict(f.split("=", 1) for f in answer(vm, "osk state")[0].split()[1:])
    return int(fields["line"]), fields


def test_read_edit_history_cancel(board, uos_image, need_mtools, tmp_path):
    with alone(uos_image, board, uos_card(tmp_path)) as vm:
        wait_ready(vm)
        start_read(vm, "- 40 Name:", 1)
        row, _ = line_row(vm)
        # A second read while one is active is refused.
        assert vm.script("readline - 10 Other:", timeout=TIMEOUT).status == "err busy"
        answer(vm, "text hello")
        for name in ("left", "left"):
            key(vm, name)
        answer(vm, "char X")
        key(vm, "f1")
        key(vm, "f3")
        key(vm, "f2")
        key(vm, "back")
        # The screen shows the prompt and the edited line on the input row.
        dump_until(vm, lambda rows: rows[row] == "Name: elXl")
        key(vm, "select")
        m = wait_marker(vm, "line result=ok", 1)
        assert (m["len"], m["text"]) == ("4", '"elXl"'), m

        # History: the line comes back, an edit of it is a new line.
        start_read(vm, "- 40 Name:", 2)
        key(vm, "up")
        key(vm, "back")
        key(vm, "select")
        m = wait_marker(vm, "line result=ok", 2)
        assert m["text"] == '"elX"', m
        start_read(vm, "- 40 Name:", 3)
        key(vm, "up")
        key(vm, "up")
        key(vm, "select")
        m = wait_marker(vm, "line result=ok", 3)
        assert m["text"] == '"elXl"', m

        # Words with spaces, through `type`, which ends with SELECT.
        start_read(vm, "- 40 Cmd:", 4)
        answer(vm, "type show time")
        m = wait_marker(vm, "line result=ok", 4)
        assert (m["len"], m["text"]) == ("9", '"show%20time"'), m

        # Ctrl-C from the serial console cancels: the text is dropped.
        start_read(vm, "- 40 Name:", 5)
        answer(vm, "text abc")
        vm.send("\x03")
        m = wait_marker(vm, "line result=cancel", 1)
        assert (m["len"], m["text"]) == ("0", '""'), m

        # The per-call maximum: the sixth character is refused.
        start_read(vm, "- 5 Max:", 6)
        answer(vm, "text abcdefgh")
        key(vm, "select")
        m = wait_marker(vm, "line result=ok", 5)
        assert (m["len"], m["text"]) == ("5", '"abcde"'), m


def test_masked_read(board, uos_image, need_mtools, tmp_path):
    with alone(uos_image, board, uos_card(tmp_path)) as vm:
        wait_ready(vm)
        start_read(vm, "m 32 Password:", 1)
        row, _ = line_row(vm)
        answer(vm, "text s3cret")
        rows = dump_until(vm, lambda rows: rows[row] == "Password: ******")
        assert not any("s3cret" in r for r in rows), rows
        key(vm, "select")
        m = wait_marker(vm, "line result=ok", 1)
        assert (m["len"], m["text"]) == ("6", "masked"), m
        assert "s3cret" not in vm.serial
        # A masked line is never stored: the history is still empty.
        start_read(vm, "- 32 Next:", 2)
        key(vm, "up")
        key(vm, "select")
        m = wait_marker(vm, "line result=ok", 2)
        assert (m["len"], m["text"]) == ("0", '""'), m


def test_osk_types_a_word(board, uos_image, need_mtools, tmp_path):
    with alone(uos_image, board, uos_card(tmp_path)) as vm:
        wait_ready(vm)
        before = rows_of(vm)
        start_read(vm, "- 40 Word:", 1)
        row, _ = line_row(vm)
        assert row < OSK_FIRST, row
        key(vm, "f4")
        wait_marker(vm, "osk state=open", 1)
        rows = dump_until(vm, lambda rows: rows[OSK_FIRST].split() == list("ABCDEFGHIJKLM"))
        assert rows[ROWS - 1].split() == ["SHIFT", "BKSP", "LEFT", "RIGHT", "ENTER", "CLOSE"]
        check_golden(rows[row:row + 1] + rows[OSK_FIRST - 1:], "console_osk")
        # A on layer 0; SHIFT; b on layer 1; BKSP; d on layer 1; ENTER.
        for name in ("select", "up", "select", "down", "select", "up", "right", "select",
                     "down", "select"):
            key(vm, name)
        rows = dump_until(vm, lambda rows: rows[row] == "Word: Ad")
        assert rows[OSK_FIRST].split() == list("abcdefghijklm"), rows
        _, fields = line_row(vm)
        assert (fields["open"], fields["layer"]) == ("1", "1"), fields
        for name in ("up", "right", "right", "right", "select"):
            key(vm, name)
        m = wait_marker(vm, "line result=ok", 1)
        assert (m["len"], m["text"]) == ("2", '"Ad"'), m
        wait_marker(vm, "osk state=closed", 1)
        # The keyboard is gone and the first screen is whole again.
        dump_until(vm, lambda rows: rows == before)


def test_osk_open_close_keeps_the_read(board, uos_image, need_mtools, tmp_path):
    with alone(uos_image, board, uos_card(tmp_path)) as vm:
        wait_ready(vm)
        before = rows_of(vm)
        assert vm.script("osk open", timeout=TIMEOUT).status == "err no line"
        start_read(vm, "h 40 Word:", 1)
        row, _ = line_row(vm)
        answer(vm, "osk open")
        wait_marker(vm, "osk state=open", 1)
        dump_until(vm, lambda rows: rows[OSK_FIRST].split() == list("ABCDEFGHIJKLM"))
        # Serial typing goes on while the keyboard is open.
        answer(vm, "text ok")
        answer(vm, "osk close")
        wait_marker(vm, "osk state=closed", 1)
        after = dump_until(vm, lambda rows: rows[row] == "Word: ok")
        assert after[:row] + after[row + 1:] == before[:row] + before[row + 1:]
        # BACK on the open keyboard closes it and keeps the line.
        key(vm, "f4")
        wait_marker(vm, "osk state=open", 2)
        key(vm, "back")
        wait_marker(vm, "osk state=closed", 2)
        key(vm, "select")
        m = wait_marker(vm, "line result=ok", 1)
        assert m["text"] == '"ok"', m
        dump_until(vm, lambda rows: rows == before)


def status(rows):
    return rows[ROWS - 1]


def test_pager_texts(board, uos_image, need_mtools, tmp_path):
    with alone(uos_image, board, uos_card(tmp_path)) as vm:
        wait_ready(vm)
        before = rows_of(vm)
        hints = "  UP/DN line  LT/RT page  F3/F4 ends  BACK quit"

        answer(vm, "page short")
        wait_marker(vm, "page top=0", 1)
        assert vm.script("page long", timeout=TIMEOUT).status == "err busy"
        rows = rows_of(vm)
        assert rows[:5] == [" Short test text", " A short test text for the pager.",
                            "         A tab starts this line.",
                            " Bytes . and . show as dots.", ""], rows
        assert status(rows) == "line 1 to 3 of 3" + hints
        key(vm, "back")
        wait_marker(vm, "page result=done", 1)
        dump_until(vm, lambda rows: rows == before)

        answer(vm, "page empty")
        wait_marker(vm, "page top=0", 2)
        rows = rows_of(vm)
        assert rows[1] == " (no text)", rows
        assert status(rows) == "line 0 to 0 of 0" + hints
        answer(vm, "char q")
        wait_marker(vm, "page result=done", 2)

        answer(vm, "page long")
        wait_marker(vm, "page top=0", 3)
        rows = rows_of(vm)
        check_golden(rows, "console_pager_long")
        assert status(rows) == f"line 1 to {BODY} of {LONG_LINES}" + hints
        pages = -(-(LONG_LINES - BODY) // BODY)
        for _ in range(pages):
            key(vm, "right")
        top = LONG_LINES - BODY
        vm.wait_for(f"@RC stage=uos ev=page top={top} ", timeout=TIMEOUT)
        assert status(rows_of(vm)) == f"line {top + 1} to {LONG_LINES} of {LONG_LINES}" + hints
        key(vm, "f3")
        dump_until(vm, lambda rows: status(rows) == f"line 1 to {BODY} of {LONG_LINES}" + hints)
        key(vm, "back")
        wait_marker(vm, "page result=done", 3)

        answer(vm, "page big")
        vm.wait_for(re.compile(rf"ev=page top=0 shown={BODY} lines={BIG_LINES}\r?\n"),
                    timeout=TIMEOUT)
        assert status(rows_of(vm)) == f"line 1 to {BODY} of {BIG_LINES}" + hints
        key(vm, "f4")
        vm.wait_for(f"ev=page top={BIG_LINES - BODY} ", timeout=TIMEOUT)
        rows = rows_of(vm)
        assert status(rows) == f"line {BIG_LINES - BODY + 1} to {BIG_LINES} of {BIG_LINES}" + hints
        key(vm, "back")
        wait_marker(vm, "page result=done", 4)
        dump_until(vm, lambda rows: rows == before)


def test_command_errors(board, uos_image, need_mtools, tmp_path):
    with alone(uos_image, board, uos_card(tmp_path)) as vm:
        wait_ready(vm)
        for cmd, err in (("readline", "err usage readline <flags> <max> [prompt]"),
                         ("readline x 10 P:", "err bad flags"),
                         ("readline - 0 P:", "err bad max"),
                         ("readline - 256 P:", "err bad max"),
                         ("page", "err usage page <short|long|empty|big>"),
                         ("page other", "err unknown text"),
                         ("osk", "err usage osk <open|close|state>"),
                         ("osk shut", "err usage osk <open|close|state>"),
                         ("osk close", "err no line")):
            assert vm.script(cmd, timeout=TIMEOUT).status == err, cmd
        line = answer(vm, "osk state")[0]
        assert line == "osk open=0 layer=0 row=0 col=0 line=0 reading=0", line
        assert "line result" not in vm.serial


def echo_bytes(vm, start):
    """The PL011 text since `start` without the marker lines and line ends:
    what the read screen echoed for a serial terminal."""
    text = vm.serial[start:]
    text = text[:text.rindex("\n") + 1]
    return "".join(line for line in text.split("\n") if not line.startswith("@RC "))


# Each redraw: CR, the prompt, the window, erase to the end of the line, and
# when the cursor is not at the end, CR, the prompt and the text up to it.
EL = "\x1b[K"


def test_serial_echo(board, uos_image, need_mtools, tmp_path):
    with alone(uos_image, board, uos_card(tmp_path)) as vm:
        wait_ready(vm)
        start = len(vm.serial)
        start_read(vm, "- 40 Name:", 1)
        answer(vm, "text abc")
        key(vm, "left")
        key(vm, "back")
        key(vm, "select")
        wait_marker(vm, "line result=ok", 1)
        assert echo_bytes(vm, start) == (
            "\rName: " + EL + "\rName: a" + EL + "\rName: ab" + EL + "\rName: abc" + EL +
            "\rName: ab" + "\rName: ac" + EL + "\rName: a"), repr(echo_bytes(vm, start))

        start = len(vm.serial)
        start_read(vm, "m 32 Password:", 2)
        answer(vm, "text s3c")
        key(vm, "select")
        wait_marker(vm, "line result=ok", 2)
        assert echo_bytes(vm, start) == (
            "\rPassword: " + EL + "\rPassword: *" + EL + "\rPassword: **" + EL +
            "\rPassword: ***" + EL), repr(echo_bytes(vm, start))
        assert "s3c" not in vm.serial
