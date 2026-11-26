"""The shell's programs: RUN HOLOTAPE and the HOLOTAPE verb open the reader and
BACK brings the prompt back; RUN HACK from a session shows the game's refusal;
LOGIN with a user name asks only for the password; RUN with no name and a bad
name; MAIL is an unknown verb."""

from test_screens import at_prompt, at_shell_prompt, settled_rows, stack
from test_shell import PASSWORDS, expect, n_marks, wait_marker, wait_state
import pytest

from uos import alone, answer, batch, card, card_tree, dump_until, key


def guest_shell(vm):
    at_prompt(vm)
    answer(vm, "sess login GUEST")
    at_shell_prompt(vm)


def holo_state(vm):
    settled_rows(vm)
    return dict(f.split("=", 1) for f in answer(vm, "holo state")[0].split()[1:])


def sess_state(vm):
    return dict(f.split("=", 1) for f in answer(vm, "sess state")[0].split()[1:])


def test_run_holotape_and_back(board, hooks_image, need_mtools, tmp_path):
    """RUN HOLOTAPE ends at once and opens the reader on the list; BACK closes it
    and the prompt comes back. HOLOTAPE PROCS opens that tape."""
    with alone(hooks_image, board, card(tmp_path, card_tree())) as vm:
        guest_shell(vm)
        _, end, _ = expect(vm, "RUN HOLOTAPE", "NORMAL")
        assert end["verb"] == "RUN", end
        m = wait_marker(vm, "holo op=program", 1)
        assert (m["tape"], m["rc"]) == ("-", "0"), m
        wait_marker(vm, "holo op=scan", 1)
        # The run ended: the shell's prompt waits under the reader.
        assert stack(vm) == ["shell", "readline", "holo"]
        assert holo_state(vm)["screen"] == "tapes"
        key(vm, "back")
        wait_marker(vm, "holo op=exit", 1)
        assert stack(vm) == ["shell", "readline"]
        wait_state(vm, state="prompt", kind="cmd")
        # The verb with a tape name opens the tape.
        expect(vm, "HOLOTAPE PROCS", "NORMAL")
        m = wait_marker(vm, "holo op=program", 2)
        assert (m["tape"], m["rc"]) == ("PROCS", "0"), m
        dump_until(vm, lambda r: r[0] == "UOS HOLOTAPE READER")
        st = holo_state(vm)
        assert (st["screen"], st["open"]) == ("entries", "PROCS.HTP"), st
        key(vm, "back")
        key(vm, "back")
        wait_marker(vm, "holo op=exit", 2)
        assert stack(vm) == ["shell", "readline"]
        wait_state(vm, state="prompt", kind="cmd")
        # An entry is not a parameter.
        expect(vm, "HOLOTAPE PROCS ONE", "MAXPARM", "%DCL-W-MAXPARM")
        assert n_marks(vm, "holo op=program") == 2


def test_run_hack_from_a_session(board, hooks_image, need_mtools, tmp_path):
    """RUN HACK opens the game's screen, which in a session refuses with busy: no
    game, no win and no lockout. BACK brings the prompt back and the session
    goes on."""
    with alone(hooks_image, board, card(tmp_path, card_tree())) as vm:
        guest_shell(vm)
        expect(vm, "RUN HACK", "NORMAL")
        assert wait_marker(vm, "hack op=program", 1)["rc"] == "0"
        m = wait_marker(vm, "hack op=start", 1)
        assert (m["result"], m["why"]) == ("refused", "busy"), m
        assert stack(vm)[-1] == "hack"
        key(vm, "back")
        assert wait_marker(vm, "hack op=end", 1)["result"] == "left"
        assert stack(vm) == ["shell", "readline"]
        wait_state(vm, state="prompt", kind="cmd")
        st = sess_state(vm)
        assert (st["state"], st["user"], st["locked_s"]) == ("session", "GUEST", "0"), st
        assert n_marks(vm, "lockout state=start") == 0
        assert n_marks(vm, "session state=start") == 1


def test_login_with_a_user_name(board, hooks_image, need_mtools, tmp_path):
    """LOGIN GUEST ends the session and asks only the password: a wrong one counts
    and brings USERNAME back; the right one starts GUEST's session."""
    with alone(hooks_image, board, card(tmp_path, card_tree())) as vm:
        at_prompt(vm)
        answer(vm, "sess login ADMIN")
        at_shell_prompt(vm)
        res, _, _ = expect(vm, "LOGIN GUEST", "NORMAL")
        wait_marker(vm, "session state=end", 1)
        wait_marker(vm, "login state=user given=yes", 1)
        wait_marker(vm, "login state=pass", 1)
        assert n_marks(vm, "login state=user rc=0") == 1
        dump_until(vm, lambda r: any(row.startswith("USERNAME: GUEST") for row in r))
        answer(vm, "type WRONG-ONE")
        m = wait_marker(vm, "login user=GUEST", 1)
        assert (m["result"], m["left"]) == ("bad", "2"), m
        wait_marker(vm, "login state=user rc=0", 2)
        assert sess_state(vm)["tries"] == "2"   # tries left
        # A right password, through LOGIN again.
        answer(vm, "sess login ADMIN")
        wait_marker(vm, "session screen=shell", 2)
        wait_state(vm, state="prompt", kind="cmd")
        expect(vm, "LOGIN guest", "NORMAL")
        wait_marker(vm, "login state=pass", 2)
        answer(vm, "type " + PASSWORDS["GUEST"])
        m = wait_marker(vm, "login user=GUEST", 2)
        assert m["result"] == "ok", m
        start = wait_marker(vm, "session state=start", 3)
        assert (start["user"], start["how"]) == ("GUEST", "login"), start
        # A name that breaks the rules keeps the session.
        wait_marker(vm, "session screen=shell", 3)
        wait_state(vm, state="prompt", kind="cmd")
        expect(vm, "LOGIN ABCDEFGHIJKLMNOPQ", "IVNAME", "%UAF-E-IVNAME")
        assert sess_state(vm)["user"] == "GUEST"


def test_run_without_a_name_and_mail(board, hooks_image, need_mtools, tmp_path):
    """RUN with no name asks for one at the prompt and fails through the API, a bad
    name is refused, and MAIL is an unknown verb."""
    with alone(hooks_image, board, card(tmp_path, card_tree())) as vm:
        guest_shell(vm)
        expect(vm, "RUN", "INSFPRM", "%DCL-W-INSFPRM")
        expect(vm, "RUN BOGUS", "IVKEYW", "%DCL-W-IVKEYW")
        expect(vm, "MAIL", "IVVERB", "%DCL-W-IVVERB")
        expect(vm, "MAIL DIRECTORY", "IVVERB", "%DCL-W-IVVERB")
        e = n_marks(vm, "cmd state=end")
        answer(vm, "type RUN")
        wait_state(vm, state="prompt", kind="param")
        dump_until(vm, lambda r: any(row.startswith("_Program:") for row in r))
        answer(vm, "type HACK")
        end = wait_marker(vm, "cmd state=end", e + 1)
        assert (end["verb"], end["rc"]) == ("RUN", "NORMAL"), end
        wait_marker(vm, "hack op=start", 1)
        key(vm, "back")
        wait_marker(vm, "hack op=end", 1)
        wait_state(vm, state="prompt", kind="cmd")


# A program's start posts its screen. A logout or a new login can run before
# that call: `sh park` holds the call so the order is the same every run. The
# call then finds its session gone: it marks "sh stale=post" and opens nothing.
GONE = [
    ("RUN HACK", "hack op=program"),
    ("RUN HOLOTAPE", "holo op=program"),
    ("RUN TERMINAL", "tui result="),
]
IDS = ["hack", "holotape", "terminal"]


def run_parked(vm, line):
    answer(vm, "sh park 1")
    expect(vm, line, "NORMAL")


def released(vm, marker):
    answer(vm, "sh park 0")
    wait_marker(vm, "sh stale=post", 1)
    assert n_marks(vm, marker) == 0
    assert n_marks(vm, "hack op=start") == 0


@pytest.mark.parametrize("line,marker", GONE, ids=IDS)
def test_program_after_logout(board, hooks_image, need_mtools, tmp_path, line, marker):
    """RUN <program>, then sess logout before its posted call: the call is
    dropped, the login prompt stays alone and the game does not start."""
    with alone(hooks_image, board, card(tmp_path, card_tree())) as vm:
        guest_shell(vm)
        run_parked(vm, line)
        answer(vm, "sess logout")
        wait_marker(vm, "login state=user", 2)
        released(vm, marker)
        assert stack(vm) == ["login", "readline"], stack(vm)
        assert sess_state(vm)["state"] != "session"


@pytest.mark.parametrize("user", ["ADMIN", "GUEST"])
@pytest.mark.parametrize("line,marker", GONE, ids=IDS)
def test_program_after_a_new_login(board, hooks_image, need_mtools, tmp_path, line, marker,
                                   user):
    """A logout and a new login before the call, of another user or of GUEST
    again: the call belongs to the old session and does not act in the new one."""
    with alone(hooks_image, board, card(tmp_path, card_tree())) as vm:
        guest_shell(vm)
        run_parked(vm, line)
        answer(vm, "sess logout")
        answer(vm, "sess login " + user)
        wait_marker(vm, "session screen=shell", 2)
        wait_state(vm, state="prompt", kind="cmd")
        released(vm, marker)
        assert stack(vm) == ["shell", "readline"], stack(vm)
        assert sess_state(vm)["user"] == user


@pytest.mark.parametrize("line,marker", GONE, ids=IDS)
def test_program_after_login_user(board, hooks_image, need_mtools, tmp_path, line, marker):
    """LOGIN ADMIN before the call: GUEST's session ends and the call does not
    act at ADMIN's password prompt."""
    with alone(hooks_image, board, card(tmp_path, card_tree())) as vm:
        guest_shell(vm)
        run_parked(vm, line)
        wait_state(vm, state="prompt", kind="cmd")
        expect(vm, "LOGIN ADMIN", "NORMAL")
        wait_marker(vm, "login state=pass", 1)
        released(vm, marker)
        assert stack(vm)[0] == "login" and len(stack(vm)) == 2, stack(vm)
