"""The screen stack's redraw rule: a screen that opens over another and closes
leaves the display as it was, cell for cell. Each test dumps the whole text grid
before the screen opens and after it closes, both with every input event and
posted call handled."""

import time

from uos import (TIMEOUT, alone, answer, batch, card, card_tree, dump, events, state, wait_marker,
                 wait_ready)

CFG = b"[system]\nname = TEST TERMINAL\n"
# The lockout's countdown row, ROW_USER in src/session/sesscreen.c.
ROW_COUNTDOWN = 7


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
