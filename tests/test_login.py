"""The session service and the login screen: the default users, wrong and unknown
logins, the lockout and its end, a damaged and a missing user database, logout and
login again, the on-screen keyboard by buttons, the masked echo on the PL011, the
idle logout, the test-only `sess login`, and no secret on the serial lines."""

import configparser
import os
import re
import struct
import time

import pytest

from test_panic import nm
from uos import (ROOT, TICK_HZ, TIMEOUT, alone, answer, card, dump_until, events, key,
                 one, state, uos_card, wait_marker, wait_ready)

UAF_PATH = "/UOS/SYSUAF.DAT"
DEFAULT_UAF = open(os.path.join(ROOT, "card", "UOS", "SYSUAF.DAT"), "rb").read()
USERS_INI = os.path.join(ROOT, "tools", "default_users.ini")

# Passwords of the default card, docs/modules/session.md.
_ini = configparser.ConfigParser()
_ini.read(USERS_INI)
DEFAULT_USERS = {name: _ini[name]["password"] for name in _ini.sections()}
# Privilege bits, src/kernel/uos_privs.h, and flags, src/session/uaf.h.
EXPECT = {"ADMIN": ("1", "2"), "OPER": ("2", "2"), "GUEST": ("0", "0")}

# LG_STEP_BLOCKS in src/session/login.h: compressions per loop pass.
STEP_BLOCKS = 256
# A step may take this long on a loaded host before the test calls it a stall.
# QEMU runs a whole 3999-compression check in about 25 ms (docs/TESTING.md).
STEP_BOUND_US = 100_000
# The welcome shows for WELCOME_TICKS in src/session/screens.c.
WELCOME_TICKS = 150
LOOP_LATENCY_TICKS = 1
HOST_ALLOWANCE_TICKS = 20

# The first line of the shell after a login, src/shell/shell.c.
SHELL_BANNER = "command shell. Type HELP for the commands."
# The message rows, src/session/login.c.
DENIED = "ACCESS DENIED. {} ATTEMPT{} LEFT."
ROW_MSG = 6


def uaf_card(tmp_path, uaf=DEFAULT_UAF, cfg=b"[system]\nname = TEST TERMINAL\n", name="sd.img"):
    extra = {} if uaf is None else {UAF_PATH: uaf}
    return uos_card(tmp_path, cfg=cfg, extra=extra, name=name)


def sess_state(vm):
    """`sess state` as a dict, once the input layer has handed every event over."""
    deadline = time.monotonic() + TIMEOUT
    while True:
        line = answer(vm, "sess state")[0]
        fields = dict(f.split("=", 1) for f in line.split()[1:])
        if fields["pending"] == "0":
            return fields
        assert time.monotonic() < deadline, line
        time.sleep(0.05)


def n_marks(vm, prefix):
    head = prefix.split()
    return len([m for m in vm.markers() if m.get("stage") == "uos" and m.get("ev") == head[0]
                and all(m.get(k) == v for k, v in (f.split("=", 1) for f in head[1:]))])


def prompt(vm, which, n):
    """Waits for the n-th `login state=<which>` marker of this boot."""
    return wait_marker(vm, f"login state={which}", n)


def login(vm, user, password, expect="ok"):
    """Types a user name and a password at the prompts; returns the result marker."""
    before = n_marks(vm, "login user=" + user.upper())
    deadline = time.monotonic() + TIMEOUT
    while sess_state(vm)["state"] != "user" or n_marks(vm, "login state=user") == 0:
        assert time.monotonic() < deadline
        time.sleep(0.05)
    n_pass = n_marks(vm, "login state=pass")
    answer(vm, f"type {user}")
    wait_marker(vm, "login state=pass", n_pass + 1)
    answer(vm, f"type {password}")
    m = wait_marker(vm, "login user=" + user.upper(), before + 1)
    assert m["result"] == expect, m
    return m


def wait_user_prompt(vm):
    deadline = time.monotonic() + TIMEOUT
    while sess_state(vm)["state"] != "user":
        assert time.monotonic() < deadline
        time.sleep(0.05)


def secrets(uaf):
    """Salts and hashes of every record, as hex in both cases."""
    count = struct.unpack_from("<H", uaf, 12)[0]
    out = []
    for i in range(count):
        rec = uaf[32 + 96 * i:32 + 96 * (i + 1)]
        for blob in (rec[16:32], rec[32:64]):
            out += [blob.hex(), blob.hex().upper()]
    return out


def test_default_users_logout_and_login_again(board, uos_image, need_mtools, tmp_path):
    """Each default user logs in, sees the shell, and logs out: ADMIN with the
    LOGOUT command, the others from the port. Logging in again works. The whole serial log
    then holds no password, no salt and no hash, and the check runs in steps."""
    with alone(uos_image, board, uaf_card(tmp_path)) as vm:
        wait_ready(vm)
        assert one(vm, "ready")["screen"] == "login"
        db = one(vm, "sessdb")
        assert (db["result"], db["users"], db["iterations"]) == ("ok", "3", "2000"), db
        assert one(vm, "sesscfg")["result"] == "ok"
        assert one(vm, "mmu")["result"] == "on"
        n = 0
        for round_ in range(2):
            for user, password in DEFAULT_USERS.items():
                n += 1
                m = login(vm, user, password)
                blocks = 1 + 1999 * (1 if len(password) <= 7 else 2)
                assert int(m["steps"]) == -(-blocks // STEP_BLOCKS), m
                assert int(m["maxstep_us"]) < STEP_BOUND_US, m
                print(f"\nlogin board={board} user={user} len={len(password)} "
                      f"steps={m['steps']} ticks={m['ticks']} us={m['us']} "
                      f"maxstep_us={m['maxstep_us']}")
                start = wait_marker(vm, "session state=start", n)
                assert (start["user"], start["how"]) == (user, "login"), start
                wait_marker(vm, "session screen=shell", n)
                st = sess_state(vm)
                assert (st["state"], st["user"], st["privs"], st["flags"], st["home"]) == \
                    ("session", user, EXPECT[user][0], EXPECT[user][1], user), st
                assert state(vm)["user"] == user
                dump_until(vm, lambda r: any(SHELL_BANNER in row for row in r))
                if user == "ADMIN":
                    # The shell's LOGOUT, typed through the line editor.
                    wait_marker(vm, "sh state=prompt kind=cmd", n)
                    answer(vm, "type LOGOUT")
                    reason = "user"
                else:
                    answer(vm, "sess logout")
                    reason = "maint"
                end = wait_marker(vm, "session state=end", n)
                assert (end["user"], end["reason"]) == (user, reason), end
                wait_user_prompt(vm)
                st = sess_state(vm)
                assert (st["user"], st["privs"], st["flags"], st["home"]) == \
                    ("none", "0", "0", "none"), st
                assert state(vm)["user"] == "none"
                dump_until(vm, lambda r: r[ROW_MSG].startswith("SESSION CLOSED."))
        # A logout with nobody logged in changes nothing.
        answer(vm, "sess logout")
        wait_marker(vm, "session result=none", 1)
        log = vm.serial + vm.serial2
        for user, password in DEFAULT_USERS.items():
            # GUEST's password is its name, which the screen shows: only its
            # password field is checked.
            assert password not in log or password == user, password
            assert f"PASSWORD: {password}" not in log
        for blob in secrets(DEFAULT_UAF):
            assert blob not in log, blob


def test_wrong_password_and_unknown_user(board, uos_image, need_mtools, tmp_path):
    """A wrong password and an unknown user give the same marker, the same message and
    the same work; each counts once."""
    with alone(uos_image, board, uaf_card(tmp_path)) as vm:
        wait_ready(vm)
        bad = login(vm, "ADMIN", "WRONG-PASSWORD!", expect="bad")
        rows = dump_until(vm, lambda r: r[ROW_MSG] == DENIED.format(2, "S"))
        unknown = login(vm, "NOBODY", "WRONG-PASSWORD!", expect="bad")
        rows2 = dump_until(vm, lambda r: r[ROW_MSG] == DENIED.format(1, ""))
        assert (bad["left"], unknown["left"]) == ("2", "1")
        assert bad["steps"] == unknown["steps"], (bad, unknown)
        assert set(bad) == set(unknown)
        assert rows[:ROW_MSG] == rows2[:ROW_MSG]
        # A good login clears the count.
        good = login(vm, "GUEST", "GUEST")
        assert good["left"] == "3", good
        log = vm.serial + vm.serial2
        assert "WRONG-PASSWORD!" not in log


def test_lockout_and_its_end(board, uos_image, need_mtools, tmp_path):
    """Two failures with retries = 2 lock the login for lockout_s = 3: input is
    refused, the countdown updates once a second, then the login comes back."""
    cfg = b"[system]\nname = TEST TERMINAL\n[login]\nretries = 2\nlockout_s = 3\n"
    with alone(uos_image, board, uaf_card(tmp_path, cfg=cfg)) as vm:
        wait_ready(vm)
        assert one(vm, "sesscfg")["retries"] == "2"
        login(vm, "GUEST", "nope", expect="bad")
        login(vm, "GUEST", "nope", expect="bad")
        start = wait_marker(vm, "lockout state=start", 1)
        assert start["s"] == "3", start
        rows = dump_until(vm, lambda r: r[ROW_MSG].startswith("TERMINAL LOCKED"))
        assert any(r.startswith("TRY AGAIN IN ") for r in rows), rows
        # Input is refused: typing does nothing and no prompt starts.
        answer(vm, "type GUEST")
        st = sess_state(vm)
        assert (st["state"], st["tries"]) == ("locked", "0"), st
        assert int(st["locked_s"]) >= 1, st
        end = wait_marker(vm, "lockout state=end", 1)
        took = int(end["t"]) - int(start["t"])
        assert 3 * TICK_HZ <= took <= 3 * TICK_HZ + LOOP_LATENCY_TICKS + HOST_ALLOWANCE_TICKS, \
            (start, end)
        left = [int(m["s"]) for m in events(vm, "lockout") if "state" not in m]
        assert left and left == sorted(left, reverse=True) and min(left) >= 1, left
        dump_until(vm, lambda r: r[ROW_MSG].startswith("TERMINAL UNLOCKED"))
        # The count starts again and a good login works.
        good = login(vm, "GUEST", "GUEST")
        assert good["left"] == "2", good


def test_damaged_database(board, uos_image, need_mtools, tmp_path):
    """A bad CRC: the system screen says the database is damaged and what to do,
    no login runs, `state` reports the error, and F4 restarts."""
    bad = bytearray(DEFAULT_UAF)
    bad[40] ^= 0xFF
    with alone(uos_image, board, uaf_card(tmp_path, uaf=bytes(bad))) as vm:
        wait_ready(vm)
        db = one(vm, "sessdb")
        assert (db["result"], db["why"]) == ("damaged", "CRC"), db
        assert one(vm, "ready")["screen"] == "first"
        assert state(vm)["error"] == "userdb"
        rows = dump_until(vm, lambda r: "USER DATABASE DAMAGED (CRC). NO ONE CAN LOG IN." in r)
        assert "RESTORE /UOS/SYSUAF.DAT FROM A BACKUP, THEN PRESS F4 TO RESTART." in rows
        assert sess_state(vm)["state"] == "off"
        assert vm.script("type GUEST", timeout=TIMEOUT).status == "ok"
        assert not events(vm, "login")
        key(vm, "f4")
        vm.wait_for(re.compile(r"@RC stage=uos ev=reboot source=user\r?\n"), timeout=TIMEOUT)


@pytest.mark.parametrize("case", ["missing", "short", "nocard", "notree"])
def test_missing_database_and_card(board, uos_image, need_mtools, tmp_path, case):
    """No database, a short file, no card and no /UOS folder: each is a named state,
    the system screen says so and no login runs."""
    sd = {"missing": lambda: uaf_card(tmp_path, uaf=None),
          "short": lambda: uaf_card(tmp_path, uaf=DEFAULT_UAF[:20]),
          "nocard": lambda: None,
          "notree": lambda: card(tmp_path, {"/ROBCO/README.TXT": b"x\n"})}[case]()
    want = {"missing": ("missing", "USER DATABASE MISSING. NO ONE CAN LOG IN."),
            "short": ("damaged", "USER DATABASE DAMAGED (SHORT). NO ONE CAN LOG IN."),
            "nocard": ("nocard", "LOGIN IS NOT AVAILABLE WITHOUT A UOS CARD."),
            "notree": ("notree", "LOGIN IS NOT AVAILABLE: NO /UOS FOLDER.")}[case]
    with alone(uos_image, board, sd) as vm:
        wait_ready(vm)
        assert one(vm, "sessdb")["result"] == want[0]
        assert one(vm, "ready")["screen"] == "first"
        dump_until(vm, lambda r: want[1] in r)
        assert sess_state(vm)["db"] == want[0]
        assert state(vm)["error"] == ("userdb" if case == "short" else
                                      "none" if case == "missing" else state(vm)["error"])


def test_cancel_and_back_on_an_empty_name(board, uos_image, need_mtools, tmp_path):
    """Ctrl-C and BACK on an empty user name, an empty name and Ctrl-C on the
    password count nothing and bring the user name prompt back."""
    with alone(uos_image, board, uaf_card(tmp_path)) as vm:
        wait_ready(vm)
        prompt(vm, "user", 1)
        key(vm, "back")
        vm.send("\x03", port=0)                 # Ctrl-C on the PL011
        prompt(vm, "user", 2)
        answer(vm, "type")                      # an empty line: SELECT alone
        prompt(vm, "user", 3)
        answer(vm, "type GUEST")
        prompt(vm, "pass", 1)
        vm.send("\x03", port=0)
        prompt(vm, "user", 4)
        dump_until(vm, lambda r: r[ROW_MSG] == "LOGIN CANCELLED.")
        st = sess_state(vm)
        assert (st["state"], st["tries"], st["user"]) == ("user", "3", "none"), st
        assert not [m for m in events(vm, "login") if "result" in m]


def test_masked_echo_on_the_pl011(board, uos_image, need_mtools, tmp_path):
    """The PL011 shows the user name as typed and one '*' per password character."""
    with alone(uos_image, board, uaf_card(tmp_path)) as vm:
        wait_ready(vm)
        login(vm, "GUEST", "GUEST")
        wait_marker(vm, "session screen=shell", 1)
        assert "USERNAME: GUEST" in vm.serial
        assert "PASSWORD: *****" in vm.serial
        assert "PASSWORD: ******" not in vm.serial
        assert "WELCOME, GUEST." in vm.serial
        assert SHELL_BANNER in vm.serial


def osk_moves(text):
    """Button presses that type `text` on layer 0 of the on-screen keyboard, from
    the top-left key, src/osk/osk.c."""
    rows = ["ABCDEFGHIJKLM", "NOPQRSTUVWXYZ", "0123456789./:"]
    r = c = 0
    out = []
    for ch in text:
        tr = next(i for i, row in enumerate(rows) if ch in row)
        tc = rows[tr].index(ch)
        out += ["down"] * max(tr - r, 0) + ["up"] * max(r - tr, 0)
        out += ["right"] * max(tc - c, 0) + ["left"] * max(c - tc, 0)
        out.append("select")
        r, c = tr, tc
    return out, r


def osk_type_and_enter(vm, text, n_open):
    answer(vm, "button down")                   # a button opens the keyboard
    wait_marker(vm, "osk state=open", n_open)
    moves, row = osk_moves(text)
    for name in moves + ["down"] * (4 - row):
        answer(vm, f"button {name}")
    fields = dict(f.split("=", 1) for f in answer(vm, "osk state")[0].split()[1:])
    deadline = time.monotonic() + TIMEOUT
    while fields["row"] != "4":
        assert time.monotonic() < deadline, fields
        time.sleep(0.05)
        fields = dict(f.split("=", 1) for f in answer(vm, "osk state")[0].split()[1:])
    col = int(fields["col"])
    for name in ["right"] * max(4 - col, 0) + ["left"] * max(col - 4, 0) + ["select"]:
        answer(vm, f"button {name}")             # ENTER is key 4 of the function row
    wait_marker(vm, "osk state=closed", n_open)


def test_login_with_buttons(board, hooks_image, need_mtools, tmp_path):
    """GPIO buttons only: a button opens the on-screen keyboard at each prompt, and
    the keyboard types the user name and the masked password."""
    with alone(hooks_image, board, uaf_card(tmp_path)) as vm:
        wait_ready(vm)
        prompt(vm, "user", 1)
        osk_type_and_enter(vm, "GUEST", 1)
        prompt(vm, "pass", 1)
        osk_type_and_enter(vm, "GUEST", 2)
        m = wait_marker(vm, "login user=GUEST", 1)
        assert m["result"] == "ok", m
        wait_marker(vm, "session screen=shell", 1)


def test_sess_login_is_test_only(board, uos_image, hooks_image, need_mtools, tmp_path):
    """`sess login` sets a session with no password in test builds only."""
    assert "uos_test_sess_login" not in nm(uos_image)
    assert "uos_test_sess_login" in nm(hooks_image)
    with alone(uos_image, board, uaf_card(tmp_path)) as vm:
        wait_ready(vm)
        assert vm.script("sess login ADMIN", timeout=TIMEOUT).status == \
            "err usage sess state|logout"
        assert sess_state(vm)["user"] == "none"
    with alone(hooks_image, board, uaf_card(tmp_path, name="sd2.img")) as vm:
        wait_ready(vm)
        prompt(vm, "user", 1)
        answer(vm, "sess login oper")
        start = wait_marker(vm, "session state=start", 1)
        assert (start["user"], start["how"]) == ("OPER", "test"), start
        wait_marker(vm, "session screen=shell", 1)
        assert sess_state(vm)["privs"] == "2"
        answer(vm, "sess login GUEST")
        wait_marker(vm, "session result=busy", 1)
        answer(vm, "sess logout")
        wait_marker(vm, "session state=end", 1)
        answer(vm, "sess login NOBODY")
        wait_marker(vm, "session result=nouser", 1)


def test_idle_logout(board, hooks_image, need_mtools, tmp_path):
    """[login] idle_s = 2: a session with no key ends after 2 s; a key starts the
    count again."""
    cfg = b"[system]\nname = TEST TERMINAL\n[login]\nidle_s = 2\n"
    with alone(hooks_image, board, uaf_card(tmp_path, cfg=cfg)) as vm:
        wait_ready(vm)
        assert one(vm, "sesscfg")["idle_s"] == "2"
        prompt(vm, "user", 1)
        answer(vm, "sess login GUEST")
        wait_marker(vm, "session state=start", 1)
        time.sleep(1.2)
        key(vm, "down")                         # the shell's prompt: no history yet
        t_key = time.monotonic()
        end = wait_marker(vm, "session state=end", 1)
        # The guest gets the key after the host sent it: never earlier.
        assert time.monotonic() - t_key >= 1.9
        assert end["reason"] == "idle", end
        wait_user_prompt(vm)
