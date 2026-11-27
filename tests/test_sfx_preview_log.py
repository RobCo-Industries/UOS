"""Saves the serial log of one terminal session with the sound on, for `make sfx-preview`.

It runs only with --sfx-preview-log PATH. The card has `[audio] sound = on`,
`trace = on` and the password game on. The session has five parts, each
after a `wait` on the maintenance port, so the guest's own clock leaves
some quiet between them:

1. A wrong password for ADMIN, then the right one.
2. One typed command in the shell, DIRECTORY.
3. OPER's menu, down three rows.
4. The password game with five cursor moves.
5. A wrong word, then the right word.

Each typed key comes 120 ms after the one before, so each key sound plays
whole. The host waits for markers between steps, which only makes a gap longer.
A silent boot first plays the same seeded game to learn its password.
RobCo-Core's tools/audio_preview.py turns the log into a WAV file.
"""

import time

import pytest

from test_hack import SEED, at_login, cfg, guess, hack_card, move_to, select, start_from_login
from test_hack import state as hack_state
from test_login import DEFAULT_USERS, n_marks, wait_user_prompt
from test_menu import wait_menu
from test_shell import marks
from test_sound import check_ramp
from uos import alone, answer, batch, wait_marker, wait_ready

AUDIO_CFG = "[audio]\nsound = on\nvolume = 80\ntrace = on\n"
# The longest sound is under 0.2 s, so a 700 ms wait leaves about 0.5 s of quiet.
GAP = "wait 700"
STEP = "wait 150"
# A key sound lasts about 103 ms. A key during one gets no sound of its own.
KEY_STEP = "wait 120"


def gap(vm):
    answer(vm, GAP)


def run_all(vm, commands):
    for line, _ in batch(vm, commands):
        assert line.startswith("ok"), (commands, line)


def typed(vm, text):
    """Types text one key at a time at a person's pace, then SELECT."""
    cmds = []
    for c in text:
        cmds += [f"char {c}", KEY_STEP]
    run_all(vm, cmds + ["key select"])


def login(vm, user, password, expect):
    """A login typed key by key, as test_login.login() does it with `type`."""
    done = n_marks(vm, "login user=" + user)
    wait_user_prompt(vm)
    asked = n_marks(vm, "login state=pass")
    typed(vm, user)
    wait_marker(vm, "login state=pass", asked + 1)
    typed(vm, password)
    m = wait_marker(vm, "login user=" + user, done + 1)
    assert m["result"] == expect, m


def wait_quiet(vm, quiet_s=1.0, limit_s=30.0):
    """Waits until no audio record has come for quiet_s."""
    end = time.monotonic() + limit_s
    seen, last = vm.serial.count("ev=audio "), time.monotonic()
    while time.monotonic() < end:
        time.sleep(0.25)
        now = vm.serial.count("ev=audio ")
        if now != seen:
            seen, last = now, time.monotonic()
        elif time.monotonic() - last >= quiet_s:
            return
    pytest.fail("the sounds did not stop\n" + vm.serial)


def password(board, image, tmp_path):
    """The game's password for SEED, from a silent boot: the same seed gives
    the same board, so the session can pick one wrong word and then the
    right one."""
    with alone(image, board, hack_card(tmp_path, cfg(), name="find.img")) as vm:
        wait_ready(vm)
        at_login(vm)
        answer(vm, f"hack seed {SEED}")
        start_from_login(vm)
        g = hack_state(vm)
        for n, s in enumerate(sorted(g.words), 1):
            if guess(vm, s, n)["result"] == "win":
                return g.words[s]
    raise AssertionError("no win")


def test_save_session_log(request, board, hooks_image, need_mtools, tmp_path):
    path = request.config.getoption("--sfx-preview-log")
    secret = password(board, hooks_image, tmp_path)
    with alone(hooks_image, board, hack_card(tmp_path, cfg(extra=AUDIO_CFG))) as vm:
        wait_ready(vm)
        check_ramp(vm)
        at_login(vm)
        answer(vm, f"hack seed {SEED}")
        gap(vm)

        # 1. A wrong password, then the right one.
        login(vm, "ADMIN", "WRONG", "bad")
        gap(vm)
        login(vm, "ADMIN", DEFAULT_USERS["ADMIN"], "ok")
        wait_marker(vm, "session screen=shell", 1)
        gap(vm)

        # 2. One typed command.
        n = len(marks(vm, "cmd state=end"))
        typed(vm, "DIRECTORY")
        wait_marker(vm, "cmd state=end", n + 1)
        gap(vm)

        # 3. The menu, down three rows. The switch to OPER makes no sound.
        answer(vm, "sess logout")
        wait_user_prompt(vm)
        answer(vm, "sess login OPER")
        wait_marker(vm, "session screen=menu", 1)
        wait_menu(vm, state="menu", level="1")
        gap(vm)
        run_all(vm, ["key down", STEP, "key down", STEP, "key down", GAP])

        # 4. The password game, five cursor moves.
        answer(vm, "sess logout")
        wait_user_prompt(vm)
        start_from_login(vm)
        g = hack_state(vm)
        gap(vm)
        run_all(vm, ["hack move right 1", STEP] * 4 + ["hack move right 1", GAP])

        # 5. A wrong word, then the right one.
        wrong = min(s for s, w in g.words.items() if w != secret)
        right = next(s for s, w in g.words.items() if w == secret)
        move_to(vm, wrong)
        gap(vm)
        select(vm)
        m = wait_marker(vm, "hack op=guess", 1)
        assert m["result"] == "denied", m
        gap(vm)
        move_to(vm, right)
        gap(vm)
        select(vm)
        assert wait_marker(vm, "hack op=guess", 2)["result"] == "win"
        gap(vm)
        wait_quiet(vm)
        log = vm.serial
    with open(path, "w", encoding="utf-8") as f:
        f.write(log)
