"""The terminal menu: the screen after the login for a user whose home holds
TERMINAL.CFG, the shell for one without; each entry type; nesting and BACK; an
entry hidden by privileges; each failure leaves the menu usable; logout; the menu
covered by another screen and shown again, with a held key across the change;
RUN TERMINAL from the shell; a refused root file; one golden of the menu's text and its drawn frame."""

import time

from uos import (TIMEOUT, alone, answer, card, card_tree, check_golden, dump, dump_until,
                 screenshot, wait_ready)
from test_shell import marks, wait_marker

CFG = b"[system]\nname = TEST TERMINAL\n"

MENU = b"""[terminal]
title = Test console
header = HEAD.TXT

[entry 1]
label = Read the log
type = text
target = LOG.TXT

[entry 2]
label = Sub menu
type = menu
target = SUB.CFG

[entry 3]
label = Show the system
type = command
target = SHOW SYSTEM

[entry 4]
label = Yard lights
type = device
target = LIGHT1 TOGGLE

[entry 5]
label = Tape list
type = holotape

[entry 6]
label = One tape
type = holotape
target = STORY.HTP

[entry 7]
label = Security only
type = command
target = SHOW TIME
privs = 4

[entry 8]
label = Missing text
type = text
target = GONE.TXT

[entry 9]
label = Missing menu
type = menu
target = NONE.CFG

[entry 10]
label = Bad menu
type = menu
target = BAD.CFG

[entry 11]
label = Missing tape
type = holotape
target = NOPE.HTP

[entry 12]
label = Failing command
type = command
target = TYPE NOSUCH.TXT

[entry 13]
label = Missing device
type = device
target = NODEV ON

[entry 14]
label = Delete with confirm
type = command
target = DELETE JUNK.TXT /CONFIRM

[entry 15]
label = Run the menu again
type = command
target = RUN TERMINAL

[entry 16]
label = Log out
type = logout
"""

SUB = b"""[terminal]
title = Sub
[entry 1]
label = Read the log here
type = text
target = LOG.TXT
[entry 2]
label = Deeper
type = menu
target = DEEP.CFG
"""

DEEP = b"""[terminal]
title = Deep
[entry 1]
label = Deeper still
type = menu
target = DEEP.CFG
"""

LABELS = ["Read the log", "Sub menu", "Show the system", "Yard lights", "Tape list", "One tape",
          "Missing text", "Missing menu", "Bad menu", "Missing tape", "Failing command",
          "Missing device", "Delete with confirm", "Run the menu again", "Log out"]


def menu_card(tmp_path):
    """OPER and ADMIN get the test menu; GUEST has none."""
    files = card_tree({"/UOS/UOS.CFG": CFG})
    for home in ("OPER", "ADMIN"):
        base = f"/UOS/HOME/{home}/"
        files.update({base + "TERMINAL.CFG": MENU, base + "SUB.CFG": SUB,
                      base + "DEEP.CFG": DEEP, base + "BAD.CFG": b"[terminal\ntitle = x\n",
                      base + "HEAD.TXT": b"Test header line one\r\nLine two\r\n",
                      base + "LOG.TXT": b"Log line one\r\nLog line two\r\n",
                      base + "JUNK.TXT": b"junk\r\n"})
    return card(tmp_path, files)


def fields(line):
    return dict(f.split("=", 1) for f in line.split()[1:])


def menu_state(vm):
    """`menu state` once every input event and posted call has run."""
    deadline = time.monotonic() + TIMEOUT
    while True:
        st = fields(answer(vm, "menu state")[0])
        if st["pending"] == "0":
            return st
        assert time.monotonic() < deadline, st
        time.sleep(0.05)


def wait_menu(vm, **want):
    deadline = time.monotonic() + TIMEOUT
    while True:
        st = menu_state(vm)
        if all(st.get(k) == v for k, v in want.items()):
            return st
        assert time.monotonic() < deadline, (want, st)
        time.sleep(0.05)


def unquote(text):
    import re
    return re.sub(r"%([0-9A-F]{2})", lambda m: chr(int(m.group(1), 16)), text)


def labels(vm):
    return [line.split(" label=", 1)[1] for line in answer(vm, "menu list")]


def pick(vm, label):
    """Selects the entry by label and chooses it, through the port."""
    i = labels(vm).index(label)
    answer(vm, f"menu pick {i}")


def stack(vm):
    menu_state(vm)
    return fields(answer(vm, "screens")[0])["stack"].split(",")


def login_menu(vm, user="OPER"):
    wait_ready(vm)
    wait_marker(vm, "login state=user", 1)
    n = len(marks(vm, "session screen=menu"))
    answer(vm, f"sess login {user}")
    wait_marker(vm, "session screen=menu", n + 1)
    return wait_menu(vm, state="menu", level="1")


def act(vm, kind, n):
    """The n-th "tui act=<kind>" marker."""
    return wait_marker(vm, f"tui act={kind}", n)


def msg(vm):
    """The message row as it is, once every event ran."""
    menu_state(vm)
    return answer(vm, "menu msg")[0][len("msg "):].rstrip()


def after_command(vm):
    """Closes the output pager when the command printed more than its result, and
    returns the menu's state once it is back."""
    deadline = time.monotonic() + TIMEOUT
    while True:
        st = menu_state(vm)
        if st["state"] == "menu":
            return st
        if stack(vm)[-1] == "pager":
            answer(vm, "key back")
        assert time.monotonic() < deadline, st
        time.sleep(0.05)


def settled_rows(vm):
    menu_state(vm)
    return dump(vm)[1]


def test_login_gives_menu_or_shell(board, hooks_image, need_mtools, tmp_path):
    """OPER, with TERMINAL.CFG, gets the menu; its SECURITY entry is hidden.
    ADMIN, with SYSTEM, sees every entry. GUEST, without the file, gets the shell.
    The menu's text matches the golden and its frame is drawn in pixels."""
    with alone(hooks_image, board, menu_card(tmp_path)) as vm:
        st = login_menu(vm)
        assert wait_marker(vm, "tui op=check user=OPER", 1)["result"] == "menu"
        assert (st["file"], st["count"], st["hidden"], st["sel"], st["mode"]) == \
            ("TERMINAL.CFG", "15", "1", "0", "session"), st
        assert labels(vm) == LABELS
        assert stack(vm) == ["menu"]
        rows = dump_until(vm, lambda r: r[0].strip() == "TEST TERMINAL" and
                          r[6].strip() == "> Read the log")
        check_golden(rows, "menu_oper")
        # The rule row is a drawn line, not characters.
        assert rows[2].strip() == ""
        img = screenshot(vm, tmp_path)
        bg = img.getpixel((1, 300))
        assert img.getpixel((3, 300)) != bg and img.getpixel((636, 300)) == img.getpixel((3, 300))
        assert img.getpixel((320, 40)) == img.getpixel((3, 300))
        answer(vm, "sess logout")
        wait_marker(vm, "tui op=exit", 1)
        login_menu(vm, "ADMIN")
        st = menu_state(vm)
        assert (st["count"], st["hidden"]) == ("16", "0"), st
        assert "Security only" in labels(vm)
        answer(vm, "sess logout")
        wait_marker(vm, "tui op=exit", 2)
        wait_marker(vm, "login state=user", 3)
        answer(vm, "sess login GUEST")
        wait_marker(vm, "session screen=shell", 1)
        assert wait_marker(vm, "tui op=check user=GUEST", 1)["result"] == "shell"
        assert menu_state(vm)["state"] == "closed"


def refused_card(tmp_path, text):
    """ADMIN and OPER get the same root menu file `text`."""
    files = card_tree({"/UOS/UOS.CFG": CFG})
    for home in ("OPER", "ADMIN"):
        files[f"/UOS/HOME/{home}/TERMINAL.CFG"] = text
    return card(tmp_path, files)


def test_refused_menu_file(board, hooks_image, need_mtools, tmp_path):
    """A root menu file that does not parse: ADMIN, with SYSTEM, gets the shell
    with the BADMENU line under the banner. OPER gets the menu screen with the
    error and can only log out."""
    line = "%UOS-W-BADMENU, TERMINAL.CFG does not parse at line 1: command line opened"
    with alone(hooks_image, board, refused_card(tmp_path, b"[terminal\ntitle = x\n")) as vm:
        wait_ready(vm)
        wait_marker(vm, "login state=user", 1)
        answer(vm, "sess login ADMIN")
        wait_marker(vm, "session screen=shell", 1)
        chk = wait_marker(vm, "tui op=check user=ADMIN", 1)
        assert (chk["result"], chk["why"], chk["line"]) == ("shell", "syntax", "1"), chk
        dump_until(vm, lambda r: any(row.strip() == line for row in r))
        assert menu_state(vm)["state"] == "closed"
        answer(vm, "sess logout")
        wait_marker(vm, "login state=user", 2)
        n = len(marks(vm, "session screen=menu"))
        answer(vm, "sess login OPER")
        wait_marker(vm, "session screen=menu", n + 1)
        chk = wait_marker(vm, "tui op=check user=OPER", 1)
        assert (chk["result"], chk["why"], chk["line"]) == ("menu", "syntax", "1"), chk
        st = wait_menu(vm, state="menu", level="0")
        assert st["count"] == "0", st
        assert msg(vm) == "Bad menu file TERMINAL.CFG (line 1)"
        answer(vm, "key select")
        assert menu_state(vm)["state"] == "menu"
        answer(vm, "key back")
        wait_menu(vm, state="confirm")
        answer(vm, "key select")
        end = wait_marker(vm, "session state=end", 2)
        assert (end["user"], end["reason"]) == ("OPER", "user"), end
        wait_marker(vm, "login state=user", 3)
        assert not marks(vm, "session screen=shell")[1:]


def test_empty_menu_file(board, hooks_image, need_mtools, tmp_path):
    """A root menu file with no entry: ADMIN gets the shell and why=empty."""
    line = "%UOS-W-BADMENU, TERMINAL.CFG has no usable entry: command line opened"
    with alone(hooks_image, board, refused_card(tmp_path, b"")) as vm:
        wait_ready(vm)
        wait_marker(vm, "login state=user", 1)
        answer(vm, "sess login ADMIN")
        wait_marker(vm, "session screen=shell", 1)
        chk = wait_marker(vm, "tui op=check user=ADMIN", 1)
        assert (chk["result"], chk["why"], chk["line"]) == ("shell", "empty", "0"), chk
        dump_until(vm, lambda r: any(row.strip() == line for row in r))


def test_no_entry_for_user(board, hooks_image, need_mtools, tmp_path):
    """Every entry needs SECURITY: OPER gets the menu screen with the reason
    and can only log out."""
    text = b"[entry 1]\nlabel = Locked\ntype = logout\nprivs = 4\n"
    with alone(hooks_image, board, refused_card(tmp_path, text)) as vm:
        wait_ready(vm)
        wait_marker(vm, "login state=user", 1)
        answer(vm, "sess login OPER")
        wait_marker(vm, "session screen=menu", 1)
        chk = wait_marker(vm, "tui op=check user=OPER", 1)
        assert (chk["result"], chk["why"]) == ("menu", "empty"), chk
        st = wait_menu(vm, state="menu")
        assert st["count"] == "0", st
        assert msg(vm) == "No menu entry for this user in TERMINAL.CFG"
        answer(vm, "key back")
        wait_menu(vm, state="confirm")
        answer(vm, "key select")
        end = wait_marker(vm, "session state=end", 1)
        assert (end["user"], end["reason"]) == ("OPER", "user"), end
        assert not marks(vm, "session screen=shell")


def test_each_entry_type(board, hooks_image, need_mtools, tmp_path):
    """text, menu, command, device and holotape each do their job and come back to
    the menu with the selection kept. Keys come from the console, a GPIO button and
    the port."""
    with alone(hooks_image, board, menu_card(tmp_path)) as vm:
        login_menu(vm)
        # Moves: a script key, a GPIO button, a typed digit.
        answer(vm, "key down")
        wait_menu(vm, sel="1")
        answer(vm, "button down")
        wait_menu(vm, sel="2")
        answer(vm, "text 1")
        wait_menu(vm, sel="0")
        # text: the pager over the menu, BACK returns.
        pick(vm, "Read the log")
        assert act(vm, "text", 1)["result"] == "ok"
        assert stack(vm) == ["menu", "pager"]
        dump_until(vm, lambda r: any("Log line one" in x for x in r))
        answer(vm, "key back")
        st = wait_menu(vm, state="menu", sel="0")
        assert stack(vm) == ["menu"]
        # command: its output in the pager, its result on the message row.
        pick(vm, "Show the system")
        end = wait_marker(vm, "cmd state=end verb=SHOW_SYSTEM", 1)
        assert end["rc"] == "NORMAL", end
        assert wait_marker(vm, "cmd state=start verb=SHOW_SYSTEM", 1)["src"] == "menu"
        assert act(vm, "command", 1)["result"] == "ok"
        assert stack(vm) == ["menu", "pager"]
        answer(vm, "key back")
        st = wait_menu(vm, state="menu", sel="2")
        assert msg(vm).startswith("%"), st
        # device: the device service acts, the new state on the message row.
        pick(vm, "Yard lights")
        d = act(vm, "device", 1)
        assert (d["result"], unquote(d["target"])) == ("ok", "LIGHT1 TOGGLE"), d
        dev = wait_marker(vm, "dev name=LIGHT1", 1)
        assert dev["by"] == "OPER", dev
        assert msg(vm) == f"LIGHT1: {dev['state']}"
        # holotape, empty target: the reader's list.
        pick(vm, "Tape list")
        assert act(vm, "holotape", 1)["result"] == "ok"
        wait_marker(vm, "holo op=scan", 1)
        assert stack(vm) == ["menu", "holo"]
        answer(vm, "key back")
        wait_marker(vm, "holo op=exit", 1)
        wait_menu(vm, state="menu", sel="4")
        # holotape, a named tape: the reader opens on it.
        pick(vm, "One tape")
        assert wait_marker(vm, "holo op=want tape=STORY.HTP", 1)["result"] == "found"
        assert wait_marker(vm, "holo op=load file=STORY.HTP", 1)["result"] == "ok"
        answer(vm, "key back")
        answer(vm, "key back")
        wait_marker(vm, "holo op=exit", 2)
        wait_menu(vm, state="menu", sel="5")
        # menu: the sub-menu and its breadcrumb.
        pick(vm, "Sub menu")
        st = wait_menu(vm, level="2", file="SUB.CFG")
        rows = dump_until(vm, lambda r: "Test console / Sub" in r[28])
        assert rows[1].strip() == "Sub"


def test_failures_leave_the_menu_usable(board, hooks_image, need_mtools, tmp_path):
    """A missing file, a bad .CFG, a missing tape, a failing command and a missing
    device each show one plain line; the menu then still works."""
    with alone(hooks_image, board, menu_card(tmp_path)) as vm:
        login_menu(vm)
        cases = [("Missing text", "text", "fail", "File GONE.TXT not found"),
                 ("Missing menu", "menu", "fail", "File NONE.CFG not found"),
                 ("Bad menu", "menu", "bad", "Bad menu file BAD.CFG (line 1)"),
                 ("Missing tape", "holotape", "missing", "No holotape NOPE.HTP")]
        n = {"text": 0, "menu": 1, "holotape": 0}
        for label, kind, result, line in cases:
            pick(vm, label)
            n[kind] += 1
            assert act(vm, kind, n[kind])["result"] == result
            st = wait_menu(vm, state="menu", level="1")
            assert msg(vm) == line, st
            assert stack(vm) == ["menu"]
            dump_until(vm, lambda r: r[27].strip() == line)
        pick(vm, "Failing command")
        c = act(vm, "command", 1)
        assert c["result"] == "fail", c
        st = after_command(vm)
        assert msg(vm).startswith("%UOS-E-"), st
        assert stack(vm) == ["menu"]
        pick(vm, "Missing device")
        assert act(vm, "device", 1)["result"] == "fail"
        st = wait_menu(vm, state="menu", level="1")
        assert msg(vm).startswith("NODEV: %"), st
        # Still usable: a text entry works.
        pick(vm, "Read the log")
        assert act(vm, "text", 2)["result"] == "ok"
        assert stack(vm) == ["menu", "pager"]
        answer(vm, "key back")
        st = wait_menu(vm, state="menu")
        assert st["msg"] == "none", st


def test_nesting_back_and_held_back(board, hooks_image, need_mtools, tmp_path):
    """Menus nest 8 deep; BACK goes up one level; a held BACK goes up once; BACK at
    the top asks, and BACK there stays."""
    with alone(hooks_image, board, menu_card(tmp_path)) as vm:
        login_menu(vm)
        pick(vm, "Sub menu")
        wait_menu(vm, level="2")
        pick(vm, "Deeper")
        wait_menu(vm, level="3", file="DEEP.CFG")
        for level in range(4, 9):
            answer(vm, "menu pick 0")
            wait_menu(vm, level=str(level))
        answer(vm, "menu pick 0")
        st = wait_menu(vm, level="8", state="menu")
        assert msg(vm) == "Menus nest 8 deep at most", st
        answer(vm, "menu back")
        wait_menu(vm, level="7")
        # A held BACK: one level, not seven.
        answer(vm, "hold back 6")
        st = wait_menu(vm, level="6", state="menu")
        for level in range(5, 0, -1):
            answer(vm, "key back")
            wait_menu(vm, level=str(level))
        answer(vm, "key back")
        st = wait_menu(vm, state="confirm", level="1")
        rows = dump_until(vm, lambda r: r[27].strip() == "Log out? ENTER: yes  BACK: no")
        answer(vm, "key back")
        wait_menu(vm, state="menu", level="1")
        assert not marks(vm, "session state=end")


def test_logout_from_the_menu(board, hooks_image, need_mtools, tmp_path):
    """BACK at the top, then ENTER, logs out; so does a logout entry."""
    with alone(hooks_image, board, menu_card(tmp_path)) as vm:
        login_menu(vm)
        answer(vm, "key back")
        wait_menu(vm, state="confirm")
        answer(vm, "key select")
        end = wait_marker(vm, "session state=end", 1)
        assert (end["user"], end["reason"]) == ("OPER", "user"), end
        wait_marker(vm, "tui op=exit", 1)
        wait_marker(vm, "login state=user", 2)
        assert menu_state(vm)["state"] == "closed"
        login_menu(vm)
        pick(vm, "Log out")
        assert act(vm, "logout", 2)["result"] == "ok"
        end = wait_marker(vm, "session state=end", 2)
        assert end["reason"] == "user", end
        wait_marker(vm, "login state=user", 3)


def test_covered_menu(board, hooks_image, need_mtools, tmp_path):
    """The reader covers the menu: a pick is refused, a held BACK closes the reader
    and leaves the menu at the top level, and the grid is as before. A command whose
    step falls due under the reader waits, then asks its question over the menu."""
    with alone(hooks_image, board, menu_card(tmp_path)) as vm:
        login_menu(vm)
        before = settled_rows(vm)
        answer(vm, "holo open")
        wait_marker(vm, "holo op=scan", 1)
        assert stack(vm) == ["menu", "holo"]
        answer(vm, "menu pick 0")
        wait_marker(vm, "tui result=covered", 1)
        assert menu_state(vm)["top"] == "0"
        answer(vm, "hold back 3")
        wait_marker(vm, "holo op=exit", 1)
        st = wait_menu(vm, state="menu", level="1")
        assert stack(vm) == ["menu"]
        assert settled_rows(vm) == before
        # A step owed under the reader.
        answer(vm, "sh hold 1")
        pick(vm, "Delete with confirm")
        wait_marker(vm, "sh held=1", 1)
        answer(vm, "holo open")
        wait_marker(vm, "holo op=scan", 2)
        answer(vm, "sh hold 0")
        wait_marker(vm, "sh op=wait why=covered", 1)
        assert stack(vm) == ["menu", "holo"]
        assert not marks(vm, "sh state=prompt kind=confirm")
        answer(vm, "key back")
        wait_marker(vm, "holo op=exit", 2)
        assert wait_marker(vm, "sh op=resume", 1)["owe"] == "step"
        wait_marker(vm, "sh state=prompt kind=confirm", 1)
        assert stack(vm) == ["menu", "readline"]
        answer(vm, "type Y")
        end = wait_marker(vm, "cmd state=end verb=DELETE", 1)
        assert end["files"] == "1", end
        assert act(vm, "command", 1)["result"] == "ok"
        after_command(vm)
        assert stack(vm) == ["menu"]


def test_run_terminal(board, hooks_image, need_mtools, tmp_path):
    """RUN TERMINAL from the shell opens the menu over it in close mode. GUEST has no
    TERMINAL.CFG: one line says so, BACK asks to close, ENTER closes, and the
    shell's prompt comes back. From the menu, RUN TERMINAL fails and the menu stays."""
    with alone(hooks_image, board, menu_card(tmp_path)) as vm:
        wait_ready(vm)
        wait_marker(vm, "login state=user", 1)
        answer(vm, "sess login GUEST")
        wait_marker(vm, "session screen=shell", 1)
        wait_marker(vm, "sh state=prompt kind=cmd", 1)
        answer(vm, 'sh run "RUN%20TERMINAL"')
        wait_marker(vm, "cmd state=end verb=RUN", 1)
        st = wait_menu(vm, state="menu", mode="program")
        assert msg(vm) == "File TERMINAL.CFG not found", st
        assert stack(vm)[0] == "shell" and stack(vm)[-1] == "menu"
        dump_until(vm, lambda r: r[29].strip() == "BACK: close")
        answer(vm, "key back")
        wait_menu(vm, state="confirm")
        dump_until(vm, lambda r: r[27].strip() == "Close the menu? ENTER: yes  BACK: no")
        answer(vm, "key select")
        wait_marker(vm, "tui op=exit", 1)
        wait_marker(vm, "sh state=prompt kind=cmd", 2)
        assert stack(vm) == ["shell", "readline"]
        assert not marks(vm, "session state=end")
        answer(vm, "sess logout")
        wait_marker(vm, "login state=user", 2)
        login_menu(vm)
        pick(vm, "Run the menu again")
        c = act(vm, "command", 1)
        assert c["result"] == "fail", c
        st = after_command(vm)
        assert (st["level"], st["mode"]) == ("1", "session"), st
        assert msg(vm).startswith("%UOS-"), st


def test_default_card(board, hooks_image, need_mtools, tmp_path):
    """The shipped card: OPER gets its menu, one entry of each type, and each one
    works; ADMIN gets the shell."""
    files = card_tree({"/UOS/UOS.CFG": CFG})
    with alone(hooks_image, board, card(tmp_path, files)) as vm:
        st = login_menu(vm)
        assert (st["count"], st["hidden"]) == ("6", "0"), st
        assert [line.split()[2] for line in answer(vm, "menu list")] == \
            ["type=text", "type=menu", "type=command", "type=device", "type=holotape",
             "type=logout"]
        pick(vm, "Read the rounds checklist")
        assert act(vm, "text", 1)["result"] == "ok"
        answer(vm, "key back")
        wait_menu(vm, state="menu")
        pick(vm, "Show the system")
        assert act(vm, "command", 1)["result"] == "ok"
        after_command(vm)
        pick(vm, "Yard lights on or off")
        assert act(vm, "device", 1)["result"] == "ok"
        pick(vm, "Holotape library")
        wait_marker(vm, "holo op=scan", 1)
        answer(vm, "key back")
        wait_marker(vm, "holo op=exit", 1)
        wait_menu(vm, state="menu")
        pick(vm, "Device controls")
        wait_menu(vm, level="2", file="CONTROL.CFG")
        pick(vm, "Unlock the vault door")
        assert act(vm, "device", 2)["result"] == "ok"
        answer(vm, "key back")
        wait_menu(vm, level="1")
        pick(vm, "Log out")
        wait_marker(vm, "session state=end", 1)
        wait_marker(vm, "login state=user", 2)
        answer(vm, "sess login ADMIN")
        wait_marker(vm, "session screen=shell", 1)
