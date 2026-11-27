"""The event sounds, with `[audio] sound = on` and `trace = on`.

A login plays pass_bad on a bad password and pass_good on a good one. The
password game plays char_scroll for a cursor move, pass_bad for a denied
word, pass_good for a win and a key sound for a bracket. A menu selection
move plays char_scroll. char_scroll starts at most once per 30 ms. Each
play line is read by its CRC, from the generated header.
"""

import os
import re
import time

from test_hack import (SEED, at_login, cfg, guess, hack_card, move_to, select, start_from_login,
                       state)
from test_login import DEFAULT_USERS, login
from test_menu import DEEP, MENU, SUB, login_menu
from test_sound import check_ramp
from uos import ROOT, TICK_HZ, TIMEOUT, alone, answer, card, card_tree, wait_marker, wait_ready

AUDIO_CFG = "[audio]\nsound = on\nvolume = 40\ntrace = on\n"
KEYS = ("key_a", "key_b")
GAP_MS = 30
# The limit counts from the request, on the tick clock. A play starts at
# the next audio tick, and the trace stamps t_ms from the system timer
# then. A late tick IRQ in QEMU can move one start by up to one tick.
TICK_MS = 1000 // TICK_HZ


def crc_names():
    """{crc: sound name} for every table of the generated header."""
    with open(os.path.join(ROOT, "src", "sfx", "sfx_table.h")) as f:
        h = f.read()
    return {m.group(2): m.group(1).lower()
            for m in re.finditer(r"#define UOS_SFX_(\w+)_CRC32 0x([0-9a-f]{8})u", h)}


NAMES = crc_names()


def plays(vm):
    """The effect plays so far, as (name, t_ms), the ramp left out."""
    return [(NAMES.get(m["crc"], "?" + m["crc"]), int(m["t_ms"])) for m in vm.markers()
            if m.get("ev") == "audio" and m.get("op") == "pcm" and m.get("rate") == "37500"]


def names(vm, start=0):
    return [n for n, _ in plays(vm)[start:]]


def wait_for_play(vm, name):
    """Waits until a play of `name` shows."""
    deadline = time.monotonic() + TIMEOUT
    while name not in names(vm):
        assert time.monotonic() < deadline, plays(vm)
        time.sleep(0.05)


def wait_plays(vm, n):
    """Waits until at least n plays show."""
    deadline = time.monotonic() + TIMEOUT
    while len(plays(vm)) < n:
        assert time.monotonic() < deadline, plays(vm)
        time.sleep(0.05)


def settle(vm):
    """Lets a play requested so far reach the trace: it starts at the next
    tick, and the loop's poll prints it."""
    time.sleep(0.1)
    answer(vm, "sess state")
    time.sleep(0.05)


def check_gaps(vm):
    """No two char_scroll starts closer than 30 ms, less one tick of trace
    jitter, and a held key does start some."""
    t = [ms for n, ms in plays(vm) if n == "char_scroll"]
    assert len(t) >= 2, t
    for a, b in zip(t, t[1:]):
        assert (b - a) & 0xffffffff >= GAP_MS - TICK_MS, t


def test_bad_then_good_login(board, uos_image, need_mtools, tmp_path):
    """A bad password, then a good one: pass_bad then pass_good, with key
    sounds only around them."""
    conf = b"[system]\nname = TEST TERMINAL\n" + AUDIO_CFG.encode()
    with alone(uos_image, board, card(tmp_path, card_tree({"/UOS/UOS.CFG": conf}))) as vm:
        wait_ready(vm)
        check_ramp(vm)
        login(vm, "ADMIN", "WRONG-PASSWORD!", expect="bad")
        login(vm, "ADMIN", DEFAULT_USERS["ADMIN"])
        wait_for_play(vm, "pass_good")
        got = names(vm)
        results = [n for n in got if n not in KEYS]
        assert results[:2] == ["pass_bad", "pass_good"], got
        i, j = got.index("pass_bad"), got.index("pass_good")
        assert all(n in KEYS for n in got[i + 1:j]), got
        assert "char_scroll" not in got, got


def test_game_sounds(board, hooks_image, need_mtools, tmp_path):
    """In the game: a move gives char_scroll, a held move key never starts
    two within 30 ms, a bracket gives one key sound, a denied word gives
    pass_bad and a win pass_good, each SELECT one sound."""
    with alone(hooks_image, board, hack_card(tmp_path, cfg(extra=AUDIO_CFG))) as vm:
        wait_ready(vm)
        check_ramp(vm)
        at_login(vm)
        answer(vm, f"hack seed {SEED}")
        start_from_login(vm)
        g = state(vm)
        n = len(plays(vm))
        answer(vm, "hack move down 1")
        wait_plays(vm, n + 1)
        state(vm)
        assert names(vm, n) == ["char_scroll"], plays(vm)
        # A held key: one press and 16 repeats, twice.
        for _ in range(2):
            answer(vm, "hold down 16")
            state(vm)
            answer(vm, "hold up 16")
            state(vm)
        settle(vm)
        check_gaps(vm)
        # Every bracket until a dud: one key sound each.
        dud = False
        for k, p in enumerate(g.pairs(), 1):
            move_to(vm, p)
            settle(vm)
            n = len(plays(vm))
            select(vm)
            m = wait_marker(vm, "hack op=bracket", k)
            wait_plays(vm, n + 1)
            state(vm)
            assert names(vm, n) in (["key_a"], ["key_b"]), (m, plays(vm)[n:])
            if m["result"] == "dud":
                dud = True
                break
        assert dud
        # Guess in board order: denied words, then the win.
        words = sorted(state(vm).words)
        for k, s in enumerate(words, 1):
            move_to(vm, s)
            settle(vm)
            n = len(plays(vm))
            select(vm)
            m = wait_marker(vm, "hack op=guess", k)
            wait_plays(vm, n + 1)
            state(vm)
            want = "pass_good" if m["result"] == "win" else "pass_bad"
            assert names(vm, n) == [want], (m, plays(vm)[n:])
            if m["result"] == "win":
                break
        assert m["result"] == "win", m
        check_gaps(vm)


def menu_sound_card(tmp_path):
    conf = b"[system]\nname = TEST TERMINAL\n" + AUDIO_CFG.encode()
    files = card_tree({"/UOS/UOS.CFG": conf})
    base = "/UOS/HOME/OPER/"
    files.update({base + "TERMINAL.CFG": MENU, base + "SUB.CFG": SUB, base + "DEEP.CFG": DEEP,
                  base + "HEAD.TXT": b"Test header line one\r\nLine two\r\n",
                  base + "LOG.TXT": b"Log line one\r\nLog line two\r\n"})
    return card(tmp_path, files)


def test_menu_move(board, hooks_image, need_mtools, tmp_path):
    """A selection move in the menu gives char_scroll; a held key never
    starts two within 30 ms."""
    with alone(hooks_image, board, menu_sound_card(tmp_path)) as vm:
        check_ramp(vm)
        login_menu(vm)
        n = len(plays(vm))
        answer(vm, "key down")
        wait_plays(vm, n + 1)
        assert names(vm, n) == ["char_scroll"], plays(vm)
        for _ in range(3):
            answer(vm, "hold down 16")
            time.sleep(0.05)
            answer(vm, "hold up 16")
            time.sleep(0.05)
        settle(vm)
        check_gaps(vm)
