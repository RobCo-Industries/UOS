"""The whole chain on the shipped card: RETROS-BIOS, MFBootAgent and UOS from one
built card, then three users in one boot. GUEST uses the shell and a light, ADMIN
copies a file, OPER works the shipped menu. Every input goes through the
maintenance port."""

import configparser
import contextlib
import os
import re

from harness.pigpio import PiGpio

from test_hack import ROW_MSG, guess, likeness, state, win
from test_login import DEFAULT_USERS, SHELL_BANNER, login
from test_menu import labels, pick, wait_menu
from test_shell import expect, marks, wait_marker
from uos import BOOT_ORDER, ROOT, TIMEOUT, _mtools, after_chain, answer, card_put, dump_until, \
    events, key, one, order, wait_ready

POST = ["memory", "video", "timer", "sd", "fat", "rng", "rtc", "audio"]


def shipped(path):
    with open(os.path.join(ROOT, "card", *path.split("/")), "rb") as f:
        return f.read()


def card_file(sd, name, tmp_path):
    out = tmp_path / name.replace("/", "_")
    _mtools(sd, "mcopy", "-n", "::/" + name, os.fspath(out))
    return out.read_bytes()


def device(name):
    """The [device NAME] section of the shipped DEVICES.CFG."""
    cfg = configparser.ConfigParser(comment_prefixes=(";", "#"))
    cfg.read_string(shipped("UOS/DEVICES.CFG").decode())
    sec = cfg[f"device {name}"]
    return int(sec["gpio"]), sec["active"]


def menu_labels():
    cfg = configparser.ConfigParser(comment_prefixes=(";", "#"))
    cfg.read_string(shipped("UOS/HOME/OPER/TERMINAL.CFG").decode())
    entries = sorted((s for s in cfg.sections() if s.startswith("entry ")),
                     key=lambda s: int(s.split()[1]))
    return [cfg[s]["label"] for s in entries]


def boot_cfg(sd, tmp_path):
    """The card's BOOT.CFG: its entry names in order, the default and the timeout.

    A chain card built without BOOT_CFG has one entry, the nightly's card has
    three. The agent must report what the card holds."""
    cfg = configparser.ConfigParser(comment_prefixes=(";", "#"))
    cfg.read_string(card_file(sd, "ROBCO/BOOT.CFG", tmp_path).decode())
    names = [s.split()[1] for s in cfg.sections() if s.startswith("entry ")]
    default = cfg["boot"]["default"]
    return names, default, cfg["boot"]["timeout"], cfg[f"entry {default}"]["path"]


def hack_cfg(**keys):
    """The shipped UOS.CFG with only the given [hack] keys changed, as a person
    edits the card on a computer."""
    out, section = [], None
    for line in shipped("UOS/UOS.CFG").decode().splitlines():
        head = re.match(r"\[(\w+)\]", line)
        if head:
            section = head.group(1)
        kv = re.match(r"(\w+)\s*=", line)
        if section == "hack" and kv and kv.group(1) in keys:
            line = f"{kv.group(1)} = {keys.pop(kv.group(1))}"
        out.append(line)
    assert not keys, f"not in the shipped [hack]: {keys}"
    return ("\n".join(out) + "\n").encode()


def game_card(sd, tmp_path, attempts):
    card_put(sd, "UOS/UOS.CFG", hack_cfg(difficulty="novice", attempts=attempts, lockout_s=3),
             tmp_path)


def guesses(vm):
    return [m for m in events(vm, "hack") if m.get("op") == "guess"]


@contextlib.contextmanager
def no_password(vm):
    """Raises a failure again with every winning word blanked: a timeout report
    quotes the serial line, where a won game's guess names the password."""
    try:
        yield
    except Exception as e:
        text = str(e)
        for m in guesses(vm):
            if m["result"] == "win":
                text = text.replace(m["word"], "*" * len(m["word"]))
        raise AssertionError(text) from None


def pin_level(on, active):
    return int(on) if active == "high" else int(not on)


def test_whole_chain_terminal(board, chain_card, chain_images, need_mtools, tmp_path):
    retros, mfboot = chain_images
    with open(retros, "rb") as f:
        assert card_file(chain_card, "RETROS.IMG", tmp_path) == f.read()
    with open(mfboot, "rb") as f:
        assert card_file(chain_card, "ROBCO/MFBOOT.SYS", tmp_path) == f.read()
    names, default, timeout, path = boot_cfg(chain_card, tmp_path)
    assert default == "UOS", "the session needs a card that starts UOS"
    light_pin, light_active = device("LIGHT1")
    door_pin, door_active = device("DOOR1")

    with after_chain(retros, board, chain_card, qtest=True) as vm:
        # The first stage: a clean self test, the shipped settings, one load.
        vm.wait_for("@RC stage=bios ev=jump", timeout=TIMEOUT)
        done = one(vm, "post_done", "bios")
        assert (done["result"], done["fail"]) == ("ok", "0"), done
        assert [m["name"] for m in events(vm, "post", "bios")] == POST, vm.serial
        st = one(vm, "settings", "bios")
        assert (st["delay_ms"], st["beep"], st["scheme"]) == ("1000", "1", "green"), st
        assert one(vm, "load", "bios")["path"] == "/ROBCO/MFBOOT.SYS"
        for ev in ("fallback", "error", "shell"):
            assert not events(vm, ev, "bios"), vm.serial

        # The boot agent: trusted, no maintenance request, UOS after the countdown.
        vm.wait_for("@RC stage=bootagent ev=jump", timeout=TIMEOUT)
        assert one(vm, "handoff", "bootagent")["trusted"] == "1"
        req = one(vm, "maint_req", "bootagent")
        assert (req["result"], req["rc"]) == ("none", "0"), req
        ent = one(vm, "entries", "bootagent")
        assert (ent["count"], ent["default"], ent["timeout"]) == \
            (str(len(names)), default, timeout), ent
        cd = one(vm, "countdown", "bootagent")
        assert (cd["state"], cd["seconds"]) == ("start", timeout), cd
        ch = one(vm, "choose", "bootagent")
        assert (ch["index"], ch["name"], ch["path"], ch["via"]) == \
            (str(names.index(default) + 1), default, path, "timeout"), ch

        # UOS, trusted after the agent, at the login screen.
        wait_ready(vm)
        handoff = one(vm, "handoff")
        assert (handoff["trusted"], handoff["prev"]) == ("1", "2"), handoff
        assert one(vm, "ready")["screen"] == "login"
        db = one(vm, "sessdb")
        assert (db["result"], db["users"]) == ("ok", "3"), db
        assert [e for e in order(vm) if e in BOOT_ORDER] == BOOT_ORDER, vm.serial

        io = PiGpio(vm, board)
        assert io.get(light_pin) == pin_level(False, light_active)
        assert io.get(door_pin) == pin_level(False, door_active)

        # GUEST: the shell, three commands, a light and a refused door.
        login(vm, "GUEST", DEFAULT_USERS["GUEST"])
        start = wait_marker(vm, "session state=start", 1)
        assert (start["user"], start["how"]) == ("GUEST", "login"), start
        wait_marker(vm, "session screen=shell", 1)
        dump_until(vm, lambda rows: any(SHELL_BANNER in row for row in rows))
        for line in ("SHOW SYSTEM", "DIRECTORY", "TYPE NOTICE.TXT"):
            expect(vm, line, "NORMAL")
        expect(vm, "DEVICE LIGHT1 ON", "NORMAL")
        wait_marker(vm, "dev name=LIGHT1 state=ON", 1)
        assert io.get(light_pin) == pin_level(True, light_active)
        expect(vm, "DEVICE DOOR1 OPEN", "NOPRIV")
        assert io.get(door_pin) == pin_level(False, door_active)
        prompts = len(marks(vm, "sh state=prompt kind=cmd"))
        wait_marker(vm, "sh state=prompt kind=cmd", prompts)
        answer(vm, "type LOGOUT")
        end = wait_marker(vm, "session state=end", 1)
        assert (end["user"], end["reason"]) == ("GUEST", "user"), end

        # ADMIN: a copy in the home folder, then a logout from the port.
        login(vm, "ADMIN", DEFAULT_USERS["ADMIN"])
        wait_marker(vm, "session screen=shell", 2)
        # A copy ends with its own success code, COPIED.
        expect(vm, "COPY NOTICE.TXT NIGHT.TXT", "COPIED")
        for line in ("DIRECTORY NIGHT.TXT", "TYPE NIGHT.TXT"):
            expect(vm, line, "NORMAL")
        answer(vm, "sess logout")
        end = wait_marker(vm, "session state=end", 2)
        assert end["user"] == "ADMIN", end

        # OPER: the shipped menu, the yard lights, and the menu's logout.
        login(vm, "OPER", DEFAULT_USERS["OPER"])
        check = wait_marker(vm, "tui op=check user=OPER", 1)
        assert check["result"] == "menu", check
        wait_marker(vm, "session screen=menu", 1)
        wait_menu(vm, state="menu", level="1")
        assert labels(vm) == menu_labels()
        pick(vm, "Yard lights on or off")
        assert wait_marker(vm, "tui act=device", 1)["result"] == "ok"
        wait_marker(vm, "dev name=LIGHT1 state=OFF", 1)
        assert io.get(light_pin) == pin_level(False, light_active)
        wait_menu(vm, state="menu")
        pick(vm, "Log out")
        end = wait_marker(vm, "session state=end", 3)
        assert end["user"] == "OPER", end
        log = vm.serial + vm.serial2

    # On the host, after the power-off.
    assert card_file(chain_card, "UOS/HOME/ADMIN/NIGHT.TXT", tmp_path) == \
        shipped("UOS/HOME/ADMIN/NOTICE.TXT")
    robco = _mtools(chain_card, "mdir", "-b", "::/ROBCO").upper()
    assert "MAINT.REQ" not in robco, robco
    # GUEST's password is its name, which the screen shows: as in test_login.py,
    # only its password field is checked.
    leaked = [user for user, password in DEFAULT_USERS.items()
              if (password in log and password != user) or f"PASSWORD: {password}" in log]
    assert not leaked, f"a password of {leaked} is on the serial lines"


def test_whole_chain_game_win(board, chain_card, chain_images, need_mtools, tmp_path):
    """The shipped chain with novice, 9 attempts: F2 at the login starts the game
    seeded from the hardware, the board's words in order reach the password, and the
    win starts a GUEST session."""
    retros, _ = chain_images
    game_card(chain_card, tmp_path, 9)
    with after_chain(retros, board, chain_card) as vm:
        with no_password(vm):
            wait_ready(vm)
            wait_marker(vm, "login state=user", 1)
            key(vm, "f2")
            wait_marker(vm, "login state=hack", 1)
            start = wait_marker(vm, "hack op=start", 1)
            assert (start["result"], start["words"], start["attempts"], start["difficulty"]) == \
                ("ok", "8", "9", "novice"), start
            assert start["seed"] != "fixed", start
            dump_until(vm, lambda r: r[0].startswith("UOS PASSWORD RECOVERY"))
            win(vm)
            made = guesses(vm)
            # The last guess holds the password: no message names it.
            assert made[-1]["result"] == "win", "the last guess did not win"
            for m in made[:-1]:
                assert m["result"] == "denied", m
                assert int(m["likeness"]) == likeness(m["word"], made[-1]["word"]), \
                    f"likeness of guess {m['word']} does not match the password"
            assert wait_marker(vm, "hack op=end", 1)["result"] == "won"
            s = wait_marker(vm, "session state=start", 1)
            assert (s["user"], s["how"]) == ("GUEST", "hack"), s
            wait_marker(vm, "session screen=shell", 1)
            answer(vm, "sess logout")
            wait_marker(vm, "session state=end", 1)


def test_whole_chain_game_lockout(board, chain_card, chain_images, need_mtools, tmp_path):
    """The shipped chain with novice, 2 attempts and a 3 s lockout: two wrong
    guesses lock the terminal, the lockout ends and GUEST logs in."""
    retros, _ = chain_images
    game_card(chain_card, tmp_path, 2)
    with after_chain(retros, board, chain_card) as vm:
        with no_password(vm):
            wait_ready(vm)
            prompts = 1
            wait_marker(vm, "login state=user", prompts)
            # The hardware seeds the game: the first two words win with chance 2 in 8,
            # so ten wins in a row come about once in a million runs.
            for game in range(1, 11):
                answer(vm, "hack start")
                assert wait_marker(vm, "hack op=start", game)["attempts"] == "2"
                words = sorted(state(vm).words)
                first = guess(vm, words[0], len(guesses(vm)) + 1)
                last = first
                if first["result"] == "denied":
                    last = guess(vm, words[1], len(guesses(vm)) + 1)
                if last["result"] != "win":
                    break
                wait_marker(vm, "session state=start user=GUEST how=hack", game)
                wait_marker(vm, "session screen=shell", game)
                answer(vm, "sess logout")
                wait_marker(vm, "session state=end", game)
                prompts += 1
                wait_marker(vm, "login state=user", prompts)
            else:
                raise AssertionError("ten won games in a row")
            assert first["result"] == "denied", first
            assert (last["result"], last["attempts"]) == ("lockout", "0"), last
            assert wait_marker(vm, "hack op=end result=lost", 1)
            start = wait_marker(vm, "lockout state=start", 1)
            assert (start["why"], start["s"]) == ("hack", "3"), start
            dump_until(vm, lambda r: r[ROW_MSG] == "TERMINAL LOCKED: PASSWORD RECOVERY FAILED.")
            st = dict(x.split("=", 1) for x in answer(vm, "sess state")[0].split()[1:])
            assert st["state"] == "locked", st
            wait_marker(vm, "lockout state=end", 1)
            dump_until(vm, lambda r: r[ROW_MSG].startswith("TERMINAL UNLOCKED"))
            login(vm, "GUEST", DEFAULT_USERS["GUEST"])
            assert wait_marker(vm, "session state=start user=GUEST how=login", 1)
