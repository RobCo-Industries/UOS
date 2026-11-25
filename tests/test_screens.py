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
