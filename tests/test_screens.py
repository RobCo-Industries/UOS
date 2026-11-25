"""The screen stack's redraw rule: a screen that opens over another and closes
leaves the display as it was, cell for cell. Each test dumps the whole text grid
before the screen opens and after it closes, both with every input event and
posted call handled."""

import re
import time

from uos import (TIMEOUT, alone, answer, batch, card, card_tree, dump, events, state, wait_marker,
                 wait_ready)

CFG = b"[system]\nname = TEST TERMINAL\n"
# The lockout's countdown row, ROW_USER in src/session/sesscreen.c.
ROW_COUNTDOWN = 7
# The login's message row, ROW_MSG.
ROW_MSG = 6


def settled_rows(vm):
    """The text grid once the loop has handled every key and posted call that the
    session, the reader and the console know of."""
    deadline = time.monotonic() + TIMEOUT
    while True:
        out = batch(vm, ["sess state", "holo state", "sh state"])
        assert [status for status, _ in out] == ["ok", "ok", "ok"], out
        pending = [dict(f.split("=", 1) for f in data[0].split()[1:])["pending"]
                   for _, data in out]
        if pending == ["0", "0", "0"]:
            break
        assert time.monotonic() < deadline, out
        time.sleep(0.05)
    _, rows = dump(vm)
    return rows


def same(before, after, skip=()):
    diff = [(i, b, a) for i, (b, a) in enumerate(zip(before, after))
            if b != a and i not in skip]
    assert len(before) == len(after) and not diff, "\n".join(
        f"row {i}:\n  before {b!r}\n  after  {a!r}" for i, b, a in diff)


def at_prompt(vm):
    wait_ready(vm)
    wait_marker(vm, "login state=user", 1)


def at_shell_prompt(vm):
    """After `sess login`: the shell's `$` read waits for a command."""
    wait_marker(vm, "session screen=shell", 1)
    wait_marker(vm, "sh state=prompt kind=cmd", 1)


def reader_open_back(vm, n=1):
    answer(vm, "holo open")
    wait_marker(vm, "holo op=scan", n)
    settled_rows(vm)
    answer(vm, "key back")
    wait_marker(vm, "holo op=exit", n)


def test_reader_over_the_login_prompt(board, uos_image, need_mtools, tmp_path):
    """The finding: BACK from the reader over the user name read line gives the
    whole login screen back, not the reader with a live prompt on one row."""
    with alone(uos_image, board, card(tmp_path, card_tree())) as vm:
        at_prompt(vm)
        before = settled_rows(vm)
        assert any(r.startswith("USERNAME:") for r in before), before
        reader_open_back(vm)
        same(before, settled_rows(vm))


def test_pager_over_the_login_prompt(board, uos_image, need_mtools, tmp_path):
    with alone(uos_image, board, card(tmp_path, card_tree())) as vm:
        at_prompt(vm)
        before = settled_rows(vm)
        answer(vm, "page short")
        wait_marker(vm, "page top=0", 1)
        settled_rows(vm)
        answer(vm, "key back")
        wait_marker(vm, "page result=done", 1)
        same(before, settled_rows(vm))


def test_held_back_leaves_the_pager_only(board, hooks_image, need_mtools, tmp_path):
    """A held BACK closes the pager once. Its repeats do not reach the login read
    line below, where they would erase the typed name."""
    with alone(hooks_image, board, card(tmp_path, card_tree())) as vm:
        at_prompt(vm)
        answer(vm, "text GUE")
        before = settled_rows(vm)
        assert any(r.startswith("USERNAME: GUE") for r in before), before
        answer(vm, "page short")
        wait_marker(vm, "page top=0", 1)
        settled_rows(vm)
        answer(vm, "hold back 3")
        wait_marker(vm, "page result=done", 1)
        same(before, settled_rows(vm))


def test_keyboard_over_the_login_prompt(board, uos_image, need_mtools, tmp_path):
    with alone(uos_image, board, card(tmp_path, card_tree())) as vm:
        at_prompt(vm)
        before = settled_rows(vm)
        answer(vm, "osk open")
        wait_marker(vm, "osk state=open", 1)
        assert settled_rows(vm) != before
        answer(vm, "osk close")
        wait_marker(vm, "osk state=closed", 1)
        same(before, settled_rows(vm))


def test_reader_over_the_lockout(board, uos_image, need_mtools, tmp_path):
    """The countdown row changes each second; every other cell must match."""
    cfg = CFG + b"[login]\nretries = 1\nlockout_s = 120\n"
    with alone(uos_image, board, card(tmp_path, card_tree({"/UOS/UOS.CFG": cfg}))) as vm:
        at_prompt(vm)
        answer(vm, "type GUEST")
        wait_marker(vm, "login state=pass", 1)
        answer(vm, "type nope")
        wait_marker(vm, "lockout state=start", 1)
        before = settled_rows(vm)
        assert before[6].startswith("TERMINAL LOCKED"), before
        reader_open_back(vm)
        same(before, settled_rows(vm), skip=(ROW_COUNTDOWN,))


def test_refusal_and_pager_over_the_reader(board, hooks_image, need_mtools, tmp_path):
    """Over the reader: the game's refusal, then an entry in the pager. Each close
    gives the reader back as it was; leaving the reader gives the login back."""
    with alone(hooks_image, board, card(tmp_path, card_tree())) as vm:
        at_prompt(vm)
        login_rows = settled_rows(vm)
        answer(vm, "holo open")
        wait_marker(vm, "holo op=scan", 1)
        answer(vm, "holo pick 1")
        assert wait_marker(vm, "holo op=load", 1)["result"] == "ok"
        before = settled_rows(vm)
        out = batch(vm, ["hack start", "hack back"])
        assert [status for status, _ in out] == ["ok", "ok"], out
        assert wait_marker(vm, "hack op=start", 1)["result"] == "refused"
        wait_marker(vm, "hack op=end", 1)
        same(before, settled_rows(vm))
        answer(vm, "holo pick 0")
        assert wait_marker(vm, "holo op=show", 1)["rc"] == "0"
        settled_rows(vm)
        answer(vm, "key back")
        deadline = time.monotonic() + TIMEOUT
        while dict(f.split("=", 1) for f in answer(vm, "holo state")[0].split()[1:])["state"] \
                != "list":
            assert time.monotonic() < deadline
            time.sleep(0.05)
        same(before, settled_rows(vm))
        answer(vm, "key back")
        wait_marker(vm, "holo op=close", 1)
        answer(vm, "key back")
        wait_marker(vm, "holo op=exit", 1)
        same(login_rows, settled_rows(vm))


def test_reader_over_a_session(board, hooks_image, need_mtools, tmp_path):
    with alone(hooks_image, board, card(tmp_path, card_tree())) as vm:
        at_prompt(vm)
        answer(vm, "sess login GUEST")
        at_shell_prompt(vm)
        before = settled_rows(vm)
        assert any("command shell" in r for r in before), before
        reader_open_back(vm)
        same(before, settled_rows(vm))


def test_pager_and_keyboard_over_the_shell(board, hooks_image, need_mtools, tmp_path):
    """The shell's screen after the TYPE /PAGE pager and after the on-screen keyboard
    over its `$` read: the display comes back as it was."""
    with alone(hooks_image, board, card(tmp_path, card_tree())) as vm:
        at_prompt(vm)
        answer(vm, "sess login GUEST")
        at_shell_prompt(vm)
        before = settled_rows(vm)
        assert any("command shell" in r for r in before), before
        answer(vm, "osk open")
        wait_marker(vm, "osk state=open", 1)
        assert settled_rows(vm) != before
        answer(vm, "osk close")
        wait_marker(vm, "osk state=closed", 1)
        same(before, settled_rows(vm))
        answer(vm, "sh run \"TYPE%20NOTICE.TXT%20/PAGE\"")
        wait_marker(vm, "page top=0", 1)
        assert settled_rows(vm) != before
        answer(vm, "key back")
        wait_marker(vm, "sh run=done", 1)
        wait_marker(vm, "sh state=prompt kind=cmd", 2)
        same(before, settled_rows(vm))


def test_run_under_the_reader_is_refused(board, hooks_image, need_mtools, tmp_path):
    """A shell covered by the reader takes no `sh run`: the reply is "covered", the
    reader stays on top and the shell's prompt stays under it."""
    with alone(hooks_image, board, card(tmp_path, card_tree())) as vm:
        at_prompt(vm)
        answer(vm, "sess login GUEST")
        at_shell_prompt(vm)
        answer(vm, "holo open")
        wait_marker(vm, "holo op=scan", 1)
        settled_rows(vm)
        answer(vm, "sh run \"SHOW%20TIME\"")
        settled_rows(vm)
        assert state(vm)["screen"] == "holo"
        assert not [m for m in events(vm, "holo") if m.get("op") == "exit"]
        wait_marker(vm, "sh run=refused why=covered", 1)
        assert not [m for m in events(vm, "sh") if m.get("run") == "done"]
        answer(vm, "key back")
        wait_marker(vm, "holo op=exit", 1)
        assert state(vm)["screen"] == "readline"
        answer(vm, "sh run \"SHOW%20TIME\"")
        wait_marker(vm, "sh run=done", 1)


def test_keyboard_under_the_reader_is_refused(board, uos_image, need_mtools, tmp_path):
    """The user name read under the reader: `osk open` says "covered" and paints
    nothing over the reader."""
    with alone(uos_image, board, card(tmp_path, card_tree())) as vm:
        at_prompt(vm)
        answer(vm, "holo open")
        wait_marker(vm, "holo op=scan", 1)
        before = settled_rows(vm)
        answer(vm, "osk open")
        same(before, settled_rows(vm))
        assert not [m for m in events(vm, "osk") if m.get("state") == "open"]
        wait_marker(vm, "osk result=covered", 1)
        answer(vm, "key back")
        wait_marker(vm, "holo op=exit", 1)
        answer(vm, "osk open")
        wait_marker(vm, "osk state=open", 1)


def close_reader(vm, n=1):
    settled_rows(vm)
    assert state(vm)["screen"] == "holo"
    answer(vm, "key back")
    wait_marker(vm, "holo op=exit", n)


def test_empty_name_and_the_reader_in_one_write(board, uos_image, need_mtools, tmp_path):
    """The finding: an empty user name and `holo open` in one write. The reader
    opens before the login starts its next read. Once the reader closes, the
    login owes that read and starts it: typed keys reach a prompt again."""
    with alone(uos_image, board, card(tmp_path, card_tree())) as vm:
        at_prompt(vm)
        out = batch(vm, ["key select", "holo open"])
        assert [r[0] for r in out] == ["ok", "ok"], out
        wait_marker(vm, "holo op=scan", 1)
        close_reader(vm)
        wait_marker(vm, "login state=user", 2)
        answer(vm, "type GUEST")
        wait_marker(vm, "login state=pass", 1)
        answer(vm, "type GUEST")
        assert wait_marker(vm, "login user=GUEST", 1)["result"] == "ok"


def test_empty_line_and_the_reader_in_one_write(board, hooks_image, need_mtools, tmp_path):
    """The same race at the shell: an empty command line and `holo open` in one
    write. Once the reader closes, the shell starts the `$` read it owes."""
    with alone(hooks_image, board, card(tmp_path, card_tree())) as vm:
        at_prompt(vm)
        answer(vm, "sess login GUEST")
        at_shell_prompt(vm)
        out = batch(vm, ["key select", "holo open"])
        assert [r[0] for r in out] == ["ok", "ok"], out
        wait_marker(vm, "holo op=scan", 1)
        close_reader(vm)
        wait_marker(vm, "sh state=prompt kind=cmd", 2)
        answer(vm, "type SHOW TIME")
        wait_marker(vm, "sh state=prompt kind=cmd", 3)
        assert state(vm)["screen"] == "readline"


# The invariant: a screen on top with no command running has a read or takes
# keys itself. Each test below covers one state with the reader at the moment
# its next step is due, closes the reader and types on.

def test_reader_over_each_login_state(board, uos_image, need_mtools, tmp_path):
    """The password read, the check with a wrong password, then the welcome."""
    with alone(uos_image, board, card(tmp_path, card_tree())) as vm:
        at_prompt(vm)
        answer(vm, "type GUEST")
        wait_marker(vm, "login state=pass", 1)
        # The password read runs under the reader and takes keys once it closes.
        answer(vm, "holo open")
        close_reader(vm)
        assert state(vm)["screen"] == "readline"
        # A wrong password: the check and the next user name read are due
        # while the reader is open.
        out = batch(vm, ["type nope", "holo open"])
        assert [r[0] for r in out] == ["ok", "ok"], out
        assert wait_marker(vm, "login user=GUEST", 1)["result"] == "bad"
        close_reader(vm)
        wait_marker(vm, "login state=user", 2)
        answer(vm, "type GUEST")
        wait_marker(vm, "login state=pass", 2)
        # The welcome ends the session's reset: the reader goes with it.
        out = batch(vm, ["type GUEST", "holo open"])
        assert [r[0] for r in out] == ["ok", "ok"], out
        assert wait_marker(vm, "login user=GUEST", 2)["result"] == "ok"
        at_shell_prompt(vm)
        assert state(vm)["screen"] == "readline"


def test_reader_over_each_shell_read(board, hooks_image, need_mtools, tmp_path):
    """The continuation line and the missing parameter: the next read starts
    under the reader and takes keys once it closes."""
    with alone(hooks_image, board, card(tmp_path, card_tree())) as vm:
        at_prompt(vm)
        answer(vm, "sess login GUEST")
        at_shell_prompt(vm)
        for first, kind, rest in (("SHOW -", "contin", "TIME"), ("SHOW", "param", "TIME")):
            n = len([m for m in events(vm, "cmd") if m.get("state") == "end"])
            out = batch(vm, [f"type {first}", "holo open"])
            assert [r[0] for r in out] == ["ok", "ok"], out
            wait_marker(vm, f"sh state=prompt kind={kind}", 1)
            wait_marker(vm, "holo op=scan", 1 if kind == "contin" else 2)
            close_reader(vm, 1 if kind == "contin" else 2)
            assert state(vm)["screen"] == "readline"
            answer(vm, f"type {rest}")
            wait_marker(vm, "cmd state=end", n + 1)


# A shell job under the reader: a step that would ask, page, start a program
# or print waits while the shell is covered and goes on once the reader closes.

def run_line(line):
    return 'sh run "' + line.replace(" ", "%20") + '"'


def stack(vm):
    """The screen stack, bottom to top, once every event and posted call ran."""
    settled_rows(vm)
    fields = dict(f.split("=", 1) for f in answer(vm, "screens")[0].split()[1:])
    return fields["stack"].split(",")


def sh_fields(vm):
    return dict(f.split("=", 1) for f in answer(vm, "sh state")[0].split()[1:])


def guest_shell(vm):
    at_prompt(vm)
    answer(vm, "sess login GUEST")
    at_shell_prompt(vm)


def covered_job(vm, line):
    """`line` starts with its first step held, the reader opens, then the step is
    let go: it is due while the reader covers the shell."""
    answer(vm, "sh hold 1")
    answer(vm, run_line(line))
    wait_marker(vm, "sh held=1", 1)
    answer(vm, "holo open")
    wait_marker(vm, "holo op=scan", 1)
    answer(vm, "sh hold 0")
    assert stack(vm) == ["shell", "holo"]
    wait_marker(vm, "sh op=wait", 1)
    assert sh_fields(vm)["state"] == "running"


def test_confirm_under_the_reader_waits(board, hooks_image, need_mtools, tmp_path):
    """DELETE /CONFIRM under the reader: the question waits and opens once the
    reader closes. Ctrl-C while it waits goes to the reader, not to the job."""
    files = card_tree({"/UOS/HOME/GUEST/GONE.TXT": b"gone\r\n"})
    with alone(hooks_image, board, card(tmp_path, files)) as vm:
        guest_shell(vm)
        covered_job(vm, "DELETE GONE.TXT /CONFIRM")
        assert not [m for m in events(vm, "sh") if m.get("kind") == "confirm"]
        # Ctrl-C reaches the top screen only: the job neither ends nor asks.
        vm.send("\x03")
        assert stack(vm) == ["shell", "holo"]
        assert sh_fields(vm)["state"] == "running"
        assert not [m for m in events(vm, "cmd") if m.get("state") == "end"]
        close_reader(vm)
        assert wait_marker(vm, "sh op=resume", 1)["owe"] == "step"
        wait_marker(vm, "sh state=prompt kind=confirm", 1)
        assert stack(vm) == ["shell", "readline"]
        answer(vm, "type Y")
        done = wait_marker(vm, "sh run=done", 1)
        assert "DELETED" in done["result"], done
        wait_marker(vm, "sh state=prompt kind=cmd", 2)


def test_ctrl_c_after_the_reader_cancels(board, hooks_image, need_mtools, tmp_path):
    """The waiting question opens once the reader closes: Ctrl-C there cancels the
    command and deletes nothing."""
    files = card_tree({"/UOS/HOME/GUEST/KEEP.TXT": b"keep\r\n"})
    with alone(hooks_image, board, card(tmp_path, files)) as vm:
        guest_shell(vm)
        covered_job(vm, "DELETE KEEP.TXT /CONFIRM")
        close_reader(vm)
        wait_marker(vm, "sh state=prompt kind=confirm", 1)
        assert stack(vm) == ["shell", "readline"]
        vm.send("\x03")
        assert wait_marker(vm, "cmd state=end", 1)["rc"] == "CANCEL"
        answer(vm, run_line("DIR KEEP.TXT"))
        assert wait_marker(vm, "cmd state=end", 2)["rc"] == "NORMAL"


def test_paged_type_under_the_reader_waits(board, hooks_image, need_mtools, tmp_path):
    """TYPE /PAGE under the reader: the pager opens once the reader closes."""
    with alone(hooks_image, board, card(tmp_path, card_tree())) as vm:
        guest_shell(vm)
        covered_job(vm, "TYPE NOTICE.TXT /PAGE")
        assert not events(vm, "page")
        close_reader(vm)
        wait_marker(vm, "page top=0", 1)
        assert stack(vm) == ["shell", "pager"]
        answer(vm, "key back")
        wait_marker(vm, "sh run=done", 1)
        wait_marker(vm, "sh state=prompt kind=cmd", 2)


def test_nothing_starts_under_the_reader(board, hooks_image, need_mtools, tmp_path):
    """sh_run_line() under the reader: CREATE would ask for its first line and RUN
    would start a program. Both are refused with SH_ERR_COVERED; once the reader
    closes, CREATE from the port asks."""
    with alone(hooks_image, board, card(tmp_path, card_tree())) as vm:
        guest_shell(vm)
        answer(vm, "sh prog reg TERMINAL")
        assert wait_marker(vm, "sh prog=reg", 1)["rc"] == "0"
        answer(vm, "holo open")
        wait_marker(vm, "holo op=scan", 1)
        for n, line in enumerate(("CREATE%20NEW.TXT", "RUN%20TERMINAL"), 1):
            answer(vm, f'sh api - "{line}"')
            assert wait_marker(vm, "sh api=call", n)["rc"] == "-7"
            assert stack(vm) == ["shell", "readline", "holo"]
        assert not [m for m in events(vm, "sh") if m.get("kind") == "line"]
        assert not [m for m in events(vm, "sh") if m.get("prog") == "start"]
        close_reader(vm)
        assert stack(vm) == ["shell", "readline"]
        answer(vm, run_line("CREATE NEW.TXT"))
        wait_marker(vm, "sh state=prompt kind=line", 1)
        assert stack(vm) == ["shell", "readline"]


def test_end_under_the_reader_waits(board, hooks_image, need_mtools, tmp_path):
    """A program ends while the reader covers the shell: its result line waits,
    so nothing paints over the reader."""
    with alone(hooks_image, board, card(tmp_path, card_tree())) as vm:
        guest_shell(vm)
        answer(vm, "sh prog reg TERMINAL")
        wait_marker(vm, "sh prog=reg", 1)
        answer(vm, run_line("RUN TERMINAL"))
        wait_marker(vm, "sh prog=start", 1)
        answer(vm, "holo open")
        wait_marker(vm, "holo op=scan", 1)
        before = settled_rows(vm)
        answer(vm, "sh prog done 1 0")
        assert wait_marker(vm, "sh op=wait", 1)["owe"] == "end"
        same(before, settled_rows(vm))
        assert stack(vm) == ["shell", "holo"]
        assert not [m for m in events(vm, "sh") if m.get("run") == "done"]
        close_reader(vm)
        assert wait_marker(vm, "sh op=resume", 1)["owe"] == "end"
        done = wait_marker(vm, "sh run=done", 1)
        assert "TEST%20PROGRAM%20RUN%201%20ENDED" in done["result"] or \
            "TEST PROGRAM RUN 1 ENDED" in done["result"], done
        wait_marker(vm, "sh state=prompt kind=cmd", 2)


# The shell and its `$` read are one unit: a command the API starts under the
# read runs, and a step owed while the reader covers the read runs once the
# reader closes, with no key.

def api_job_under_the_reader(vm, line):
    answer(vm, "sh hold 1")
    answer(vm, f'sh api - "{line}"')
    assert wait_marker(vm, "sh api=call", 1)["rc"] == "0"
    wait_marker(vm, "sh held=1", 1)
    answer(vm, "holo open")
    wait_marker(vm, "holo op=scan", 1)
    answer(vm, "sh hold 0")
    assert stack(vm) == ["shell", "readline", "holo"]
    assert wait_marker(vm, "sh op=wait", 1)["owe"] == "step"
    close_reader(vm)
    assert wait_marker(vm, "sh op=resume", 1)["owe"] == "step"
    assert wait_marker(vm, "cmd state=end", 1)["rc"] == "NORMAL"
    wait_marker(vm, "sh api=done", 1)


def test_api_command_under_the_prompt_and_the_reader(board, hooks_image, need_mtools, tmp_path):
    """The finding: the step owed under the reader runs when the reader closes
    over the `$` read, not when the user ends the prompt."""
    with alone(hooks_image, board, card(tmp_path, card_tree())) as vm:
        guest_shell(vm)
        api_job_under_the_reader(vm, "TYPE%20NOTICE.TXT")
        assert stack(vm) == ["shell", "readline"]
        assert sh_fields(vm)["state"] != "running"


def test_api_command_under_the_keyboard_and_the_reader(board, hooks_image, need_mtools,
                                                       tmp_path):
    """The same with the keyboard open on the `$` read: reader over keyboard
    over read line over shell."""
    with alone(hooks_image, board, card(tmp_path, card_tree())) as vm:
        guest_shell(vm)
        answer(vm, "osk open")
        wait_marker(vm, "osk state=open", 1)
        api_job_under_the_reader(vm, "TYPE%20NOTICE.TXT")
        assert stack(vm) == ["shell", "readline"]
        assert sh_fields(vm)["state"] != "running"
        assert not [m for m in events(vm, "osk") if m.get("state") == "closed"]


# A screen that paints from a timer paints only through the stack: while it is
# covered its state moves on, the grid under the covering screen stays, and the
# stack shows the real state once the cover goes.

def lockout(vm):
    at_prompt(vm)
    answer(vm, "type GUEST")
    wait_marker(vm, "login state=pass", 1)
    answer(vm, "type nope")
    wait_marker(vm, "lockout state=start", 1)


def sess_fields(vm):
    return dict(f.split("=", 1) for f in answer(vm, "sess state")[0].split()[1:])


def test_countdown_under_the_reader(board, uos_image, need_mtools, tmp_path):
    """The countdown's timer fires under the reader: no cell of the reader
    changes. Once the reader closes, the row shows the real time left."""
    cfg = CFG + b"[login]\nretries = 1\nlockout_s = 120\n"
    with alone(uos_image, board, card(tmp_path, card_tree({"/UOS/UOS.CFG": cfg}))) as vm:
        lockout(vm)
        answer(vm, "holo open")
        wait_marker(vm, "holo op=scan", 1)
        before = settled_rows(vm)
        n = len([m for m in events(vm, "lockout") if "s" in m])
        vm.wait_for(re.compile(r"(?:@RC stage=uos ev=lockout s=\d+\r?\n[\s\S]*?){%d}" % (n + 2)),
                    timeout=TIMEOUT)
        same(before, settled_rows(vm))
        answer(vm, "key back")
        wait_marker(vm, "holo op=exit", 1)
        rows = settled_rows(vm)
        left = int(sess_fields(vm)["locked_s"])
        found = [re.search(r"TRY AGAIN IN (\d+) SECOND", r) for r in rows]
        shown = [int(m.group(1)) for m in found if m]
        assert len(shown) == 1, rows
        shown = shown[0]
        assert left <= shown <= left + 1, (rows, left)
        assert shown < 120


def test_banner_end_under_the_pager(board, uos_image, need_mtools, tmp_path):
    """With no user database the banner's end shows the first screen. A pager
    opened during the banner stays whole; the first screen comes back after."""
    files = card_tree()
    del files["/UOS/SYSUAF.DAT"]
    with alone(uos_image, board, card(tmp_path, files)) as vm:
        wait_marker(vm, "sessdb", 1)
        deadline = time.monotonic() + TIMEOUT
        while vm.script("page short", timeout=TIMEOUT).status != "ok":
            assert time.monotonic() < deadline
            time.sleep(0.01)
        wait_marker(vm, "page top=0", 1)
        wait_ready(vm)
        assert [m["ev"] for m in vm.markers() if m.get("ev") in ("page", "ready")][0] == \
            "page", "the pager opened after the banner ended"
        pager = settled_rows(vm)
        assert not any("USER DATABASE MISSING" in r for r in pager), pager
        answer(vm, "key back")
        wait_marker(vm, "page result=done", 1)
        rows = settled_rows(vm)
        assert any("USER DATABASE MISSING" in r for r in rows), rows


# A session reset while the reader covers the login or the welcome: the reset
# ends the reader with every other screen. The stack is then the session's next
# screen and its read, and keys reach that read.

def logged_in(vm):
    wait_marker(vm, "session screen=shell", 1)
    wait_marker(vm, "sh state=prompt kind=cmd", 1)
    assert stack(vm) == ["shell", "readline"]
    answer(vm, "type SHOW TIME")
    wait_marker(vm, "cmd state=end", 1)


def back_at_login(vm, n, reader=True):
    if reader:
        wait_marker(vm, "holo op=exit", 1)
    wait_marker(vm, "login state=user", n)
    assert stack(vm) == ["login", "readline"]
    assert state(vm)["screen"] == "readline"
    passes = len([m for m in events(vm, "login") if m.get("state") == "pass"])
    answer(vm, "type GUEST")
    wait_marker(vm, "login state=pass", passes + 1)


def test_lockout_ends_under_the_reader(board, uos_image, need_mtools, tmp_path):
    cfg = CFG + b"[login]\nretries = 1\nlockout_s = 3\n"
    with alone(uos_image, board, card(tmp_path, card_tree({"/UOS/UOS.CFG": cfg}))) as vm:
        lockout(vm)
        answer(vm, "holo open")
        wait_marker(vm, "holo op=scan", 1)
        assert stack(vm) == ["lockout", "holo"]
        wait_marker(vm, "lockout state=end", 1)
        back_at_login(vm, 2)


def test_welcome_ends_under_the_reader(board, uos_image, need_mtools, tmp_path):
    with alone(uos_image, board, card(tmp_path, card_tree())) as vm:
        at_prompt(vm)
        answer(vm, "type GUEST")
        wait_marker(vm, "login state=pass", 1)
        answer(vm, "type GUEST")
        wait_marker(vm, "session state=start", 1)
        answer(vm, "holo open")
        wait_marker(vm, "holo op=open", 1)
        wait_marker(vm, "session screen=shell", 1)
        order = [(m["ev"], m.get("op") or m.get("screen")) for m in vm.markers()
                 if m.get("ev") == "holo" or (m.get("ev") == "session" and "screen" in m)]
        assert order.index(("holo", "open")) < order.index(("session", "shell")), order
        logged_in(vm)


def test_idle_logout_under_the_reader(board, hooks_image, need_mtools, tmp_path):
    cfg = CFG + b"[login]\nidle_s = 2\n"
    with alone(hooks_image, board, card(tmp_path, card_tree({"/UOS/UOS.CFG": cfg}))) as vm:
        guest_shell(vm)
        answer(vm, "holo open")
        wait_marker(vm, "holo op=scan", 1)
        assert stack(vm) == ["shell", "readline", "holo"]
        assert wait_marker(vm, "session state=end", 1)["reason"] == "idle"
        back_at_login(vm, 2)


def test_logout_under_the_reader(board, hooks_image, need_mtools, tmp_path):
    with alone(hooks_image, board, card(tmp_path, card_tree())) as vm:
        guest_shell(vm)
        answer(vm, "holo open")
        wait_marker(vm, "holo op=scan", 1)
        answer(vm, "sess logout")
        assert wait_marker(vm, "session state=end", 1)["reason"] == "maint"
        back_at_login(vm, 2)


def test_logout_under_the_reader_ends_a_held_job(board, hooks_image, need_mtools, tmp_path):
    """The finding: a logout took the shell away under a job, and the job's next
    step still ran, printing over the login screen. Now the job takes no step
    and ends cancelled, and the login screen stays as the stack drew it."""
    with alone(hooks_image, board, card(tmp_path, card_tree())) as vm:
        at_prompt(vm)
        login_rows = settled_rows(vm)
        answer(vm, "sess login GUEST")
        at_shell_prompt(vm)
        answer(vm, "sh hold 1")
        answer(vm, run_line("DIRECTORY"))
        wait_marker(vm, "sh held=1", 1)
        answer(vm, "holo open")
        wait_marker(vm, "holo op=scan", 1)
        out = batch(vm, ["sess logout", "sh hold 0"])
        assert [r[0] for r in out] == ["ok", "ok"], out
        end = wait_marker(vm, "cmd state=end", 1)
        assert (end["steps"], end["rc"]) == ("0", "CANCEL"), end
        wait_marker(vm, "login state=user", 2)
        rows = settled_rows(vm)
        assert rows[ROW_MSG] == "SESSION CLOSED.", rows
        same(login_rows, rows, skip=(ROW_MSG,))
        back_at_login(vm, 2)


def test_logout_during_a_job_takes_no_more_step(board, hooks_image, need_mtools, tmp_path):
    """The finding: `sess logout` while TYPE runs. The step the job had posted
    still ran after the shell left, and printed over the login screen. Now the
    job takes no step once its shell is gone and ends cancelled."""
    text = b"".join(b"LINE %05d\n" % i for i in range(5000))
    files = card_tree({"/UOS/HOME/GUEST/LONG.TXT": text})
    with alone(hooks_image, board, card(tmp_path, files)) as vm:
        at_prompt(vm)
        login_rows = settled_rows(vm)
        answer(vm, "sess login GUEST")
        at_shell_prompt(vm)
        answer(vm, run_line("TYPE LONG.TXT"))
        wait_marker(vm, "cmd state=start", 1)
        answer(vm, "sess logout")
        assert wait_marker(vm, "cmd state=end", 1)["rc"] == "CANCEL"
        wait_marker(vm, "login state=user", 2)
        rows = settled_rows(vm)
        assert rows[ROW_MSG] == "SESSION CLOSED.", rows
        same(login_rows, rows, skip=(ROW_MSG,))
        back_at_login(vm, 2, reader=False)


# The shell takes a job only when its screen is on the stack and its unit is on
# top. Before a session's shell exists, `sh run` and `sh api` are refused and
# the screen on top stays as it was.

def refused_jobs(vm, want_run, want_rc):
    runs = len([m for m in events(vm, "sh") if m.get("run") == "refused"])
    calls = len([m for m in events(vm, "sh") if m.get("api") == "call"])
    out = batch(vm, [run_line("DIRECTORY"), 'sh api - "DIRECTORY"'])
    assert [r[0] for r in out] == ["ok", "ok"], out
    run = wait_marker(vm, "sh run=refused", runs + 1)
    call = wait_marker(vm, "sh api=call", calls + 1)
    for k, v in want_run.items():
        assert run.get(k) == v, run
    assert (call["rc"], call["cbs"]) == (want_rc, "0"), call
    assert not [m for m in events(vm, "cmd") if m.get("state") == "start"]


def test_no_job_before_the_shell(board, hooks_image, need_mtools, tmp_path):
    cfg = CFG + b"[login]\nretries = 1\nlockout_s = 3\n"
    with alone(hooks_image, board, card(tmp_path, card_tree({"/UOS/UOS.CFG": cfg}))) as vm:
        lockout(vm)
        before = settled_rows(vm)
        refused_jobs(vm, {"rc": "-3"}, "-3")
        same(before, settled_rows(vm), skip=(ROW_COUNTDOWN,))
        wait_marker(vm, "lockout state=end", 1)
        wait_marker(vm, "login state=user", 2)
        before = settled_rows(vm)
        refused_jobs(vm, {"rc": "-3"}, "-3")
        same(before, settled_rows(vm))
        # The welcome: a session, and no shell screen yet.
        answer(vm, "type GUEST")
        wait_marker(vm, "login state=pass", 2)
        answer(vm, "type GUEST")
        wait_marker(vm, "session state=start", 1)
        assert stack(vm) == ["welcome"]
        before = settled_rows(vm)
        refused_jobs(vm, {"why": "covered"}, "-7")
        assert stack(vm) == ["welcome"]
        same(before, settled_rows(vm))
        wait_marker(vm, "session screen=shell", 1)
        wait_marker(vm, "sh state=prompt kind=cmd", 1)
        answer(vm, "type SHOW TIME")
        wait_marker(vm, "cmd state=end", 1)
