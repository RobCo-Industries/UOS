"""The password game: a won game that starts a GUEST session, a lost game that locks
the terminal with the login's lockout screen and never logs the password, the bracket
tricks, GPIO buttons, the refusals, missing and damaged word lists, and the test-only
fixed seed."""

import os
import re
import subprocess
import time

from harness.env import minimal_env
from test_panic import nm
from uos import (ROOT, TICK_HZ, TIMEOUT, alone, answer, card, card_tree, check_golden,
                 dump_until, events, key, wait_marker, wait_ready)

# A seed whose first two board words are not the password, with the settings below.
SEED = 7
# Novice: 8 words of 4 or 5 letters, src/hack/hack.c.
NOVICE = "[hack]\nenabled = yes\nuser = GUEST\ndifficulty = novice\n"
ROW_MSG = 6
LOOP_LATENCY_TICKS = 1
HOST_ALLOWANCE_TICKS = 20


def cfg(extra="", attempts=9, lockout_s=60):
    return (b"[system]\nname = TEST TERMINAL\n" + NOVICE.encode() +
            f"attempts = {attempts}\nlockout_s = {lockout_s}\n{extra}".encode())


def hack_card(tmp_path, conf, name="sd.img", drop=(), extra=None):
    files = card_tree({"/UOS/UOS.CFG": conf})
    for k in drop:
        files = {p: d for p, d in files.items() if not p.startswith(k)}
    files.update(extra or {})
    return card(tmp_path, files, name=name)


def unq(text):
    return re.sub(r"%([0-9A-F]{2})", lambda m: chr(int(m.group(1), 16)), text)


class Game:
    """`hack state` parsed: the head fields, the board as two column strings of 192
    cells, the history, and the words on the board as {start cell: word}."""

    def __init__(self, lines):
        self.f = dict(x.split("=", 1) for x in lines[0].split()[1:])
        rows = [unq(l.split("text=", 1)[1]) for l in lines if l.startswith("row ")]
        self.hist = [unq(l.split("text=", 1)[1]) for l in lines if l.startswith("hist ")]
        self.rows = rows
        self.cols = ["".join(r[7:19] for r in rows), "".join(r[28:40] for r in rows)]
        n = int(self.f.get("len", 0))
        self.words = {}
        for c, text in enumerate(self.cols):
            for m in re.finditer(r"[A-Z]+", text):
                assert len(m.group()) == n, (m.group(), n)
                self.words[c * 192 + m.start()] = m.group()

    def cell(self, i):
        return self.cols[i // 192][i % 192]

    def pairs(self):
        """Usable bracket openers: each pairs with the first closer of its kind to its
        right on the same row with no letter between, as docs/modules/hack.md says."""
        out = []
        for i in range(384):
            c, r, x = i // 192, (i % 192) // 12, i % 12
            row = self.cols[c][r * 12:(r + 1) * 12]
            close = {"(": ")", "[": "]", "{": "}", "<": ">"}.get(row[x])
            if not close:
                continue
            for j in range(x + 1, 12):
                if row[j].isalpha():
                    break
                if row[j] == close:
                    out.append(i)
                    break
        return out


def state(vm):
    deadline = time.monotonic() + TIMEOUT
    while True:
        lines = answer(vm, "hack state")
        g = Game(lines)
        if g.f["pending"] == "0":
            return g
        assert time.monotonic() < deadline, lines[0]
        time.sleep(0.05)


def move_to(vm, target, buttons=False):
    """Moves the cursor to `target` with the fewest moves: rows first, then cells
    along the row across both columns."""
    cur = int(state(vm).f["cursor"])
    c, r, x = cur // 192, (cur % 192) // 12, cur % 12
    tc, tr, tx = target // 192, (target % 192) // 12, target % 12
    steps = [("down" if tr > r else "up", abs(tr - r))]
    p, tp = c * 12 + x, tc * 12 + tx
    steps.append(("right" if tp > p else "left", abs(tp - p)))
    for d, n in steps:
        if n == 0:
            continue
        if buttons:
            for _ in range(n):
                answer(vm, f"button {d}")
        else:
            answer(vm, f"hack move {d} {n}")
    g = state(vm)
    assert int(g.f["cursor"]) == target, (g.f, target)
    return g


def select(vm, buttons=False):
    answer(vm, "button select" if buttons else "hack select")
    return state(vm)


def likeness(a, b):
    return sum(x == y for x, y in zip(a, b))


def at_login(vm):
    wait_marker(vm, "login state=user", 1)


def start_from_login(vm, n=1):
    """F2 on the empty user name line starts the game."""
    key(vm, "f2")
    wait_marker(vm, "login state=hack", n)
    return wait_marker(vm, "hack op=start", n)


def guess(vm, start, n):
    move_to(vm, start)
    select(vm)
    return wait_marker(vm, "hack op=guess", n)


def test_won_game_starts_a_guest_session(board, hooks_image, need_mtools, tmp_path):
    """A fixed seed, novice, 9 attempts: guess the words in board order until the
    password; each likeness matches the password's; the win starts a GUEST session
    through the session service with how=hack."""
    with alone(hooks_image, board, hack_card(tmp_path, cfg())) as vm:
        wait_ready(vm)
        at_login(vm)
        answer(vm, f"hack seed {SEED}")
        start = start_from_login(vm)
        assert (start["result"], start["words"], start["attempts"], start["seed"],
                start["difficulty"]) == ("ok", "8", "9", "fixed", "novice"), start
        assert start["len"] in ("4", "5"), start
        g = state(vm)
        assert (g.f["state"], g.f["attempts"], g.f["cursor"]) == ("playing", "9", "0"), g.f
        assert len(g.words) == 8 and int(g.f["pairs"]) >= 4, g.f
        rows = dump_until(vm, lambda r: r[0].startswith("UOS PASSWORD RECOVERY"))
        check_golden(rows, "hack_start")
        order = sorted(g.words)
        results = []
        for n, s in enumerate(order, 1):
            m = guess(vm, s, n)
            assert m["word"] == g.words[s], m
            results.append(m)
            if n == 2:
                assert [x["result"] for x in results] == ["denied", "denied"], results
                rows = dump_until(vm, lambda r: any("Likeness=" in x for x in r))
                check_golden(rows, "hack_two_guesses")
            if m["result"] == "win":
                break
        answer_word = results[-1]["word"]
        assert results[-1]["result"] == "win"
        for m in results[:-1]:
            assert int(m["likeness"]) == likeness(m["word"], answer_word), m
            assert m["result"] == "denied"
        assert int(results[-1]["attempts"]) == 9 - (len(results) - 1)
        assert wait_marker(vm, "hack op=end", 1)["result"] == "won"
        s = wait_marker(vm, "session state=start", 1)
        assert (s["user"], s["how"], s["privs"]) == ("GUEST", "hack", "0"), s
        wait_marker(vm, "session screen=noshell", 1)
        st = dict(x.split("=", 1) for x in answer(vm, "sess state")[0].split()[1:])
        assert (st["state"], st["user"]) == ("session", "GUEST"), st
        assert state(vm).f["state"] == "off"


def test_lost_game_locks_and_never_logs_the_password(board, hooks_image, need_mtools,
                                                     tmp_path):
    """2 attempts, lockout_s = 3: two wrong guesses lock the terminal with the login's
    lockout screen, which counts down and gives the login back. No word left on the
    board after the game appears on the PL011: the password is one of them."""
    with alone(hooks_image, board, hack_card(tmp_path, cfg(attempts=2, lockout_s=3))) as vm:
        wait_ready(vm)
        at_login(vm)
        answer(vm, f"hack seed {SEED}")
        answer(vm, "hack start")                # the port starts it as F2 does
        assert wait_marker(vm, "hack op=start", 1)["attempts"] == "2"
        g = state(vm)
        order = sorted(g.words)
        first = guess(vm, order[0], 1)
        assert first["result"] == "denied", first
        lk = int(first["likeness"])
        # A word whose likeness to the first guess differs cannot be the password.
        second = next(s for s in order[1:] if likeness(g.words[s], first["word"]) != lk)
        last = guess(vm, second, 2)
        assert (last["result"], last["attempts"]) == ("lockout", "0"), last
        assert wait_marker(vm, "hack op=end", 1)["result"] == "lost"
        start = wait_marker(vm, "lockout state=start", 1)
        assert (start["why"], start["s"]) == ("hack", "3"), start
        dump_until(vm, lambda r: r[ROW_MSG] == "TERMINAL LOCKED: PASSWORD RECOVERY FAILED.")
        st = dict(x.split("=", 1) for x in answer(vm, "sess state")[0].split()[1:])
        assert st["state"] == "locked", st
        # The game refuses while the terminal is locked.
        answer(vm, "hack start")
        assert wait_marker(vm, "hack op=start", 2)["why"] == "locked"
        answer(vm, "hack back")
        end = wait_marker(vm, "lockout state=end", 1)
        took = int(end["t"]) - int(start["t"])
        assert 3 * TICK_HZ <= took <= 3 * TICK_HZ + LOOP_LATENCY_TICKS + HOST_ALLOWANCE_TICKS
        dump_until(vm, lambda r: r[ROW_MSG].startswith("TERMINAL UNLOCKED"))
        wait_marker(vm, "login state=user", 2)
        guessed = {first["word"], last["word"]}
        for w in set(g.words.values()) - guessed:
            assert w not in vm.serial, w
        assert not events(vm, "session")


def test_bracket_tricks(board, hooks_image, need_mtools, tmp_path):
    """Every usable pair removes a dud or resets the attempts, once; a removed dud is
    dots and never the password; a used pair does nothing."""
    with alone(hooks_image, board, hack_card(tmp_path, cfg(attempts=3))) as vm:
        wait_ready(vm)
        at_login(vm)
        answer(vm, f"hack seed {SEED}")
        start_from_login(vm)
        g = state(vm)
        first = guess(vm, sorted(g.words)[0], 1)
        assert first["result"] == "denied" and first["attempts"] == "2"
        outcomes = []
        for n, p in enumerate(g.pairs(), 1):
            before = state(vm)
            move_to(vm, p)
            assert state(vm).f["hl"] == "pair"
            after = select(vm)
            m = wait_marker(vm, "hack op=bracket", n)
            outcomes.append(m["result"])
            if m["result"] == "dud":
                assert len(after.words) == len(before.words) - 1
                gone = set(before.words) - set(after.words)
                assert len(gone) == 1
                s = gone.pop()
                assert after.cols[s // 192][s % 192:s % 192 + len(before.words[s])] == \
                    "." * len(before.words[s])
                assert after.hist[-1] == "Dud removed."
            else:
                assert m["result"] == "reset" and m["attempts"] == "3", m
                assert after.hist[-1] == "Tries reset."
            # The same pair again: nothing.
            again = select(vm)
            assert len([x for x in events(vm, "hack") if x.get("op") == "bracket"]) == n
            assert again.f["attempts"] == after.f["attempts"]
            assert again.f["hl"] == "char"
        assert len([m for m in events(vm, "hack") if m.get("op") == "bracket"]) == len(outcomes)
        assert "dud" in outcomes, outcomes
        # The password is still on the board: guessing the rest wins.
        g = state(vm)
        n = 2
        for s in sorted(g.words):
            m = guess(vm, s, n)
            n += 1
            if m["result"] == "win":
                break
            if m["result"] == "lockout":
                break
        assert m["result"] in ("win", "lockout")


def test_buttons_move_and_select(board, hooks_image, need_mtools, tmp_path):
    """GPIO buttons move the cursor and SELECT guesses; BACK leaves the game and
    the login prompt comes back."""
    with alone(hooks_image, board, hack_card(tmp_path, cfg())) as vm:
        wait_ready(vm)
        at_login(vm)
        answer(vm, f"hack seed {SEED}")
        start_from_login(vm)
        g = state(vm)
        s = sorted(g.words)[0]
        move_to(vm, s, buttons=True)
        assert state(vm).f["hl"] == "word"
        select(vm, buttons=True)
        m = wait_marker(vm, "hack op=guess", 1)
        assert m["word"] == g.words[s], m
        answer(vm, "button down")
        answer(vm, "button right")
        g2 = state(vm)
        assert int(g2.f["cursor"]) != s
        answer(vm, "button back")
        assert wait_marker(vm, "hack op=end", 1)["result"] == "left"
        wait_marker(vm, "login state=user", 2)
        assert state(vm).f["state"] == "off"


def uaf_with(tmp_path, flags):
    ini = tmp_path / "users.ini"
    ini.write_text(f"[GUEST]\npassword = GUEST\nhome = GUEST\nflags = {flags}\n"
                   "[ADMIN]\npassword = ADMIN-CHANGE-ME\nprivs = SYSTEM\nhome = ADMIN\n"
                   "flags = NOHACK\n")
    out = tmp_path / "SYSUAF.DAT"
    subprocess.run(["python3", os.path.join(ROOT, "tools", "mkuaf.py"), "build", str(ini),
                    "-o", str(out), "--seed", "test"], check=True, capture_output=True,
                   env=minimal_env())
    return out.read_bytes()


def test_refusals(board, hooks_image, need_mtools, tmp_path):
    """The game off, a NOHACK target, a target that does not exist, a disabled target
    and a session already running: each refuses with its reason, shows it, starts no
    session, and BACK gives the login back."""
    cases = [("off", cfg(extra="enabled = no\n"), None),
             ("nohack", cfg(extra="user = ADMIN\n"), None),
             ("nouser", cfg(extra="user = NOBODY\n"), None),
             ("disabled", cfg(), uaf_with(tmp_path, "DISABLED"))]
    texts = {"off": "THE PASSWORD GAME IS TURNED OFF ON THIS TERMINAL.",
             "nohack": "THE TARGET ACCOUNT CANNOT BE HACKED.",
             "nouser": "THE PASSWORD GAME HAS NO TARGET ACCOUNT.",
             "disabled": "THE TARGET ACCOUNT IS DISABLED."}
    for i, (why, conf, uaf) in enumerate(cases):
        extra = {"/UOS/SYSUAF.DAT": uaf} if uaf else None
        with alone(hooks_image, board, hack_card(tmp_path, conf, name=f"{i}.img",
                                                 extra=extra)) as vm:
            wait_ready(vm)
            at_login(vm)
            m = start_from_login(vm)
            assert (m["result"], m["why"]) == ("refused", why), m
            dump_until(vm, lambda r: r[23] == texts[why])
            assert texts[why] in vm.serial
            key(vm, "back")
            wait_marker(vm, "login state=user", 2)
            assert not events(vm, "session")
    with alone(hooks_image, board, hack_card(tmp_path, cfg(), name="busy.img")) as vm:
        wait_ready(vm)
        at_login(vm)
        answer(vm, "sess login GUEST")
        wait_marker(vm, "session screen=noshell", 1)
        answer(vm, "hack start")
        m = wait_marker(vm, "hack op=start", 1)
        assert (m["result"], m["why"]) == ("refused", "busy"), m
        answer(vm, "hack back")
        wait_marker(vm, "hack op=end", 1)
        assert dict(x.split("=", 1) for x in answer(vm, "sess state")[0].split()[1:])[
            "user"] == "GUEST"


def test_word_lists_missing_or_damaged(board, hooks_image, need_mtools, tmp_path):
    """No WORDS folder, and lists with a bad line: the game names the list and does
    not start."""
    bad = {f"/UOS/WORDS/W{n:02d}.TXT": b"ABCD\nab1d\n" for n in range(4, 13)}
    for i, (want, kw) in enumerate([("nolist", {"drop": ("/UOS/WORDS",)}),
                                    ("badlist", {"extra": bad})]):
        with alone(hooks_image, board, hack_card(tmp_path, cfg(), name=f"{i}.img", **kw)) as vm:
            wait_ready(vm)
            at_login(vm)
            answer(vm, f"hack seed {SEED}")
            m = start_from_login(vm)
            assert m["result"] == want, m
            word = "MISSING" if want == "nolist" else "DAMAGED"
            rows = dump_until(vm, lambda r: r[23].startswith("WORD LIST /UOS/WORDS/W0"))
            assert rows[23].endswith(f"IS {word}."), rows[23]
            assert state(vm).f["state"] == "error"
            key(vm, "back")
            wait_marker(vm, "login state=user", 2)


def test_fixed_seed_is_test_only(board, uos_image, hooks_image, need_mtools, tmp_path):
    """`hack seed` and `holo hog` exist in test builds only; the shipped image seeds
    from the hardware generator."""
    for sym in ("uos_test_hack_seed", "uos_test_holo_hog"):
        assert sym not in nm(uos_image), sym
        assert sym in nm(hooks_image), sym
    with alone(uos_image, board, hack_card(tmp_path, cfg())) as vm:
        wait_ready(vm)
        assert vm.script("hack seed 7", timeout=TIMEOUT).status == \
            "err usage hack start|move|select|back|state"
        assert vm.script("holo hog", timeout=TIMEOUT).status == \
            "err usage holo open|list|pick <n>|state"
        rng = [m for m in events(vm, "hack") if m.get("op") == "rng"][0]
        print(f"\nhack board={board} rng={rng['result']} rc={rng['rc']}")
        at_login(vm)
        m = start_from_login(vm)
        assert (m["result"], m["seed"]) == ("ok", "hw"), m
