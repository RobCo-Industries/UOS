"""The command shell: the prompt after login, every verb through `sh run`, each
qualifier, wildcards, access refusals per user, a read-only volume, a missing file,
a stepped copy cancelled with Ctrl-C, SET HALT and its forms, LOGOUT, typing through
the line editor with the continuation and missing-parameter prompts, AUTHORIZE and
SET PASSWORD with masked reads, and no password on the serial lines. /OUTPUT keeps the
old file on a failure and a Ctrl-C; a late program end is ignored; wildcards count the
refused matches; a 1 MiB line prints in bounded steps."""

import configparser
import os
import re
import time

import pytest

from harness.boot import parse_markers
from uos import (IDLE, ROOT, TIMEOUT, _mtools, alone, answer, card, card_names, card_tree,
                 dump_until, events, one, wait_ready)

REBOOTED = re.compile(r"(@RC stage=uos ev=ready [^\n]*\n[\s\S]*){2}")

_ini = configparser.ConfigParser()
_ini.read(os.path.join(ROOT, "tools", "default_users.ini"))
PASSWORDS = {name: _ini[name]["password"] for name in _ini.sections()}
CFG = b"[system]\nname = TEST TERMINAL\n"


def shell_card(tmp_path, extra=None, name="sd.img"):
    files = card_tree({"/UOS/UOS.CFG": CFG})
    files.update(extra or {})
    return card(tmp_path, files, name=name)


def n_marks(vm, prefix):
    return len(marks(vm, prefix))


def marks(vm, prefix):
    head = prefix.split()
    return [m for m in vm.markers() if m.get("stage") == "uos" and m.get("ev") == head[0]
            and all(m.get(k) == v for k, v in (f.split("=", 1) for f in head[1:]))]


def wait_marker(vm, prefix, n=1):
    """The n-th marker that matches `prefix`, by polling the parsed markers. The
    shared helper's regex grows exponentially with n while it waits: these tests
    count dozens of markers."""
    deadline = time.monotonic() + TIMEOUT
    while True:
        found = marks(vm, prefix)
        if len(found) >= n:
            return found[n - 1]
        assert time.monotonic() < deadline, (prefix, n, vm.serial[-3000:])
        time.sleep(0.02)


def sh_state(vm):
    """`sh state` once the input layer handed every event over."""
    deadline = time.monotonic() + TIMEOUT
    while True:
        line = answer(vm, "sh state")[0]
        fields = dict(f.split("=", 1) for f in line.split()[1:])
        if fields["pending"] == "0":
            return fields
        assert time.monotonic() < deadline, line
        time.sleep(0.05)


def wait_state(vm, **want):
    deadline = time.monotonic() + TIMEOUT
    while True:
        st = sh_state(vm)
        if all(st.get(k) == v for k, v in want.items()):
            return st
        assert time.monotonic() < deadline, (want, st)
        time.sleep(0.05)


def login(vm, user):
    """A session through the test-only `sess login`, then the shell's prompt."""
    n = n_marks(vm, "session screen=shell")
    answer(vm, f"sess login {user}")
    wait_marker(vm, "session screen=shell", n + 1)
    wait_state(vm, state="prompt", kind="cmd")


def quote(text):
    return '"' + "".join(c if c.isalnum() or c in "$:[].*;/=(),-_!'" else f"%{ord(c):02X}"
                         for c in text) + '"'


def unquote(text):
    text = text.strip('"')
    return re.sub(r"%([0-9A-F]{2})", lambda m: chr(int(m.group(1), 16)), text)


def run(vm, line):
    """Runs one command line through `sh run`; returns (result line, end marker, start
    offset of the serial text)."""
    n = n_marks(vm, "sh run=done")
    e = n_marks(vm, "cmd state=end")
    start = len(vm.serial)
    answer(vm, "sh run " + quote(line))
    done = wait_marker(vm, "sh run=done", n + 1)
    end = wait_marker(vm, "cmd state=end", e + 1)
    return unquote(done["result"]), end, start


def out(vm, start):
    """Serial lines since `start` that are not markers."""
    return [l.rstrip("\r") for l in vm.serial[start:].split("\n") if not l.startswith("@RC")]


def files(vm, start, op=None):
    return [(unquote(m["file"]), m["result"]) for m in parse_markers(vm.serial[start:])
            if m.get("ev") == "cmd" and "file" in m and (op is None or m["op"] == op)]



def send_line(vm, line):
    """Starts `sh run` without waiting for the end; returns the counts to wait on."""
    counts = (n_marks(vm, "sh run=done"), n_marks(vm, "cmd state=end"), len(vm.serial))
    answer(vm, "sh run " + quote(line))
    return counts


def wait_done(vm, counts):
    done = wait_marker(vm, "sh run=done", counts[0] + 1)
    end = wait_marker(vm, "cmd state=end", counts[1] + 1)
    return unquote(done["result"]), end, counts[2]


def reply(vm, kind, text, n=None):
    """Waits for the next read of `kind` a command asks, then types `text`."""
    if n is None:
        n = n_marks(vm, f"sh state=prompt kind={kind}") + 1
    wait_marker(vm, f"sh state=prompt kind={kind}", n)
    wait_state(vm, ask=kind)
    answer(vm, f"type {text}")


def expect(vm, line, rc, ident=None):
    """Runs a line and checks the end marker's code and the result line."""
    res, end, start = run(vm, line)
    assert end["rc"] == rc, (line, end, res, out(vm, start))
    if ident is not None:
        assert res.startswith(ident), (line, res)
    return res, end, start


BIG = bytes(range(256)) * 4096          # 1 MiB: 64 copy steps
# The longest command step of TYPE: the 1 MiB read, one vfs call, is the
# longest; a step of 8 rows is far shorter. 1 s at 100 Hz.
MAX_STEP_TICKS = 100


def test_show_help_and_simple_verbs(board, hooks_image, need_mtools, tmp_path):
    """SHOW and each keyword, /OUTPUT, HELP with a topic and a bad one, ROBCO, RUN,
    HOLOTAPE and MAIL with no program installed, a bad verb, SET TERMINAL."""
    with alone(hooks_image, board, shell_card(tmp_path)) as vm:
        wait_ready(vm)
        login(vm, "ADMIN")
        _, _, s = expect(vm, "SHOW DEFAULT", "NORMAL")
        assert "SD0:[HOME.ADMIN]" in out(vm, s)
        _, _, s = expect(vm, "SHOW USERS", "NORMAL")
        assert any(l.startswith("ADMIN ") and "SYSTEM" in l for l in out(vm, s)), out(vm, s)
        _, _, s = expect(vm, "SHOW SYSTEM", "NORMAL")
        assert any("on TEST TERMINAL" in l for l in out(vm, s))
        # QEMU has no clock chip: the uptime and a warning.
        res, _, s = expect(vm, "SHOW TIME", "NOCLOCK", "%UOS-W-NOCLOCK")
        assert any(l.startswith("Uptime 0 00:") for l in out(vm, s))
        _, _, s = expect(vm, "SHOW DEVICES", "NORMAL")
        o = out(vm, s)
        assert any(l.startswith("DOOR1") for l in o) and any(l.startswith("VAULT") for l in o), o
        _, _, s = expect(vm, "SH DEV LIGHT1", "NORMAL")
        assert any(l.startswith("LIGHT1") for l in out(vm, s))
        expect(vm, "SHOW DEVICES NOSUCH", "NODEV")
        _, _, s = expect(vm, "SHOW TERMINAL", "NORMAL")
        assert "Scheme GREEN" in out(vm, s)
        _, _, s = expect(vm, "SHOW MEMORY", "NORMAL")
        assert any(l.startswith("Heap ") for l in out(vm, s))
        # /OUTPUT: the text goes to a file, not the screen.
        _, _, s = expect(vm, "SHOW SYSTEM /OUTPUT=SYS.LIS", "NORMAL")
        assert not any("TEST TERMINAL" in l for l in out(vm, s))
        _, _, s = expect(vm, "TYPE SYS.LIS", "NORMAL")
        assert any("on TEST TERMINAL" in l for l in out(vm, s))
        _, _, s = expect(vm, "HELP", "NORMAL")
        assert "HELP [topic]" in out(vm, s)
        _, _, s = expect(vm, "HELP AUTH", "NORMAL")
        assert "AUTHORIZE keyword" in out(vm, s)
        expect(vm, "HELP BOGUS", "NOHELP", "%HELP-W-NOHELP")
        _, _, s = expect(vm, "ROBCO", "NORMAL")
        assert "ROBCO INDUSTRIES UNIFIED OPERATING SYSTEM" in out(vm, s)
        res, _, _ = expect(vm, "RUN HACK", "NOTINST")
        assert res == "%UOS-W-NOTINST, HACK is not installed in this build", res
        expect(vm, "RUN TERMINAL", "NOTINST")
        res, _, _ = expect(vm, "HOLOTAPE STORY", "NOTINST")
        assert "HOLOTAPE is not installed" in res, res
        res, _, _ = expect(vm, "MAIL DIRECTORY", "NOTINST")
        assert "MAIL is not installed" in res, res
        expect(vm, "RUN BOGUS", "IVKEYW")
        expect(vm, "FROB", "IVVERB", "%DCL-W-IVVERB")
        n = n_marks(vm, "term scheme=AMBER")
        expect(vm, "SET TERMINAL /SCHEME=AMBER", "NORMAL")
        wait_marker(vm, "term scheme=AMBER", n + 1)
        _, _, s = expect(vm, "SHOW TERMINAL", "NORMAL")
        assert "Scheme AMBER" in out(vm, s)
        expect(vm, "SET TERMINAL /SCHEME=RED", "IVKEYW")
        # A comment-only line from the port is a quiet success.
        expect(vm, "! nothing", "NORMAL")


def test_files_and_qualifiers(board, hooks_image, need_mtools, tmp_path):
    """DIRECTORY with each qualifier, TYPE, CREATE, COPY, RENAME and DELETE with /LOG
    and wildcards, /CONFIRM answers, SET DEFAULT, a missing file and folder."""
    with alone(hooks_image, board, shell_card(tmp_path)) as vm:
        wait_ready(vm)
        login(vm, "ADMIN")
        _, end, s = expect(vm, "DIRECTORY", "NORMAL")
        o = out(vm, s)
        assert "Directory SD0:[HOME.ADMIN]" in o and "NOTICE.TXT" in o, o
        assert "Total of 6 files." in o and end["files"] == "6", (o, end)
        _, _, s = expect(vm, "DIR *.TXT /SIZE", "NORMAL")
        o = out(vm, s)
        assert any(re.match(r"NOTICE\.TXT +\d+$", l) for l in o), o
        assert not any(l.startswith("MAINT.CFG") for l in o), o
        _, _, s = expect(vm, "DIR NOTICE.TXT /DATE", "NORMAL")
        assert any(re.match(r"NOTICE\.TXT +\d\d-[A-Z]{3}-\d{4} ", l) for l in out(vm, s))
        _, _, s = expect(vm, "DIR /FULL", "NORMAL")
        assert any(re.match(r"NOTICE\.TXT +\d+  \d\d-", l) for l in out(vm, s)), out(vm, s)
        _, _, s = expect(vm, "DIR /NOFULL /OUTPUT=LIST.LIS", "NORMAL")
        assert "Directory SD0:[HOME.ADMIN]" not in out(vm, s)
        _, _, s = expect(vm, "TYPE LIST.LIS", "NORMAL")
        assert "Directory SD0:[HOME.ADMIN]" in out(vm, s)
        expect(vm, "DIR *.XYZ", "NOFILES", "%UOS-W-NOFILES")
        expect(vm, "DIR [.NOPE]", "DNF")
        expect(vm, "TYPE NOPE.TXT", "FNF", "%UOS-E-FNF")
        expect(vm, "TYPE [.NOPE]A.TXT", "DNF")
        expect(vm, "TYPE BAD..NAME", "IVSPEC")
        # TYPE /PAGE: the pager, left with Q.
        c = send_line(vm, "TYPE NOTICE.TXT /PAGE")
        dump_until(vm, lambda r: any("line 1 to" in row for row in r))
        assert sh_state(vm)["state"] == "running"
        answer(vm, "char q")
        res, end, _ = wait_done(vm, c)
        assert (end["verb"], end["rc"], end["files"]) == ("TYPE", "NORMAL", "1"), end
        # TYPE with a wildcard shows each file under its name.
        _, end, s = expect(vm, "TYPE N*.TXT", "NORMAL")
        assert "SD0:[HOME.ADMIN]NOTICE.TXT;1" in out(vm, s), out(vm, s)
        # CREATE: lines typed at the ">" prompt, a lone period ends them.
        c = send_line(vm, "CREATE NOTE.TXT")
        reply(vm, "line", "FIRST LINE")
        reply(vm, "line", "SECOND LINE")
        reply(vm, "line", ".")
        res, end, _ = wait_done(vm, c)
        assert end["rc"] == "CREATED" and res.startswith("%CREATE-S-CREATED"), (res, end)
        _, _, s = expect(vm, "TYPE NOTE.TXT", "NORMAL")
        assert out(vm, s)[-3:-1] == ["FIRST LINE", "SECOND LINE"] or \
            ["FIRST LINE", "SECOND LINE"] == [l for l in out(vm, s) if "LINE" in l], out(vm, s)
        expect(vm, "CREATE NOTE.TXT", "EXISTS")
        # CREATE cancelled with Ctrl-C makes nothing.
        c = send_line(vm, "CREATE GONE.TXT")
        wait_state(vm, ask="line")
        vm.send("\x03")
        res, end, _ = wait_done(vm, c)
        assert end["rc"] == "CANCEL", end
        expect(vm, "TYPE GONE.TXT", "FNF")
        _, _, s = expect(vm, "CREATE [.SUB] /DIRECTORY /LOG", "CREATED")
        assert any(l.startswith("%CREATE-S-CREATED, [.SUB]") for l in out(vm, s))
        expect(vm, "CREATE [.SUB2] /DIRECTORY", "CREATED")
        expect(vm, "CREATE [.SUB] /DIRECTORY", "EXISTS")
        # COPY: one file, then a wildcard into a folder, with /LOG.
        res, end, _ = expect(vm, "COPY NOTE.TXT COPY1.TXT", "COPIED")
        assert res == "%COPY-S-COPIED, 1 file copied", res
        expect(vm, "COPY NOTE.TXT COPY1.TXT", "EXISTS")
        res, end, s = expect(vm, "COPY *.TXT [.SUB] /LOG", "COPIED")
        assert res == "%COPY-S-COPIED, 6 files copied", res
        assert sum(1 for l in out(vm, s) if l.startswith("%COPY-I-FILCOPY,")) == 6, out(vm, s)
        assert all(r == "ok" for _, r in files(vm, s, "copy"))
        # RENAME with /LOG, to another folder.
        res, _, s = expect(vm, "RENAME COPY1.TXT [.SUB2]MOVED.TXT /LOG", "RENAMED")
        assert any(l.startswith("%RENAME-I-FILREN,") for l in out(vm, s))
        _, _, s = expect(vm, "DIR [.SUB2]", "NORMAL")
        assert "MOVED.TXT" in out(vm, s)
        # SET DEFAULT into the folder, a wildcard DELETE with /LOG, and back.
        expect(vm, "SET DEFAULT [.SUB]", "NORMAL")
        _, _, s = expect(vm, "SHOW DEFAULT", "NORMAL")
        assert "SD0:[HOME.ADMIN.SUB]" in out(vm, s)
        res, end, s = expect(vm, "DELETE D*.TXT /LOG", "DELETED")
        assert res == "%DELETE-S-DELETED, 1 file deleted", res
        assert any(l.startswith("%DELETE-I-FILDEL,") for l in out(vm, s))
        # /CONFIRM: a non-answer asks again, then N, Y and A.
        c = send_line(vm, "DELETE *.TXT /CONFIRM")
        reply(vm, "confirm", "MAYBE")
        reply(vm, "confirm", "N")
        reply(vm, "confirm", "Y")
        reply(vm, "confirm", "A")
        res, end, s = wait_done(vm, c)
        assert res == "%DELETE-S-DELETED, 4 files deleted", res
        marks = files(vm, s, "delete")
        assert [r for _, r in marks] == ["skip"] + ["ok"] * 4, marks
        # Q stops the loop.
        c = send_line(vm, "DELETE *.TXT /CONFIRM")
        reply(vm, "confirm", "Q")
        res, end, _ = wait_done(vm, c)
        assert end["rc"] == "CANCEL" and end["files"] == "0", end
        expect(vm, "SET DEFAULT [-]", "NORMAL")
        expect(vm, "SET DEFAULT [.NOPE]", "DNF")
        _, _, s = expect(vm, "SHOW DEFAULT", "NORMAL")
        assert "SD0:[HOME.ADMIN]" in out(vm, s)
        expect(vm, "DELETE NOPE*.TXT", "NOFILES")
        expect(vm, "DELETE [.SUB]", "IVSPEC")


def test_access_per_user(board, hooks_image, need_mtools, tmp_path):
    """GUEST cannot read or delete in another home, run AUTHORIZE or SET HALT, or
    open a door; SYSUAF.DAT stays under DELETE *.* even for SYSTEM."""
    with alone(hooks_image, board, shell_card(tmp_path)) as vm:
        wait_ready(vm)
        login(vm, "GUEST")
        expect(vm, "DIR SD0:[HOME.ADMIN]", "NOPRIV", "%UOS-E-NOPRIV")
        expect(vm, "TYPE SD0:[HOME.ADMIN]DUTY.TXT", "NOPRIV")
        expect(vm, "TYPE SD0:[HOME.ADMIN]*.TXT", "NOPRIV")
        res, end, s = expect(vm, "DELETE SD0:[HOME.ADMIN]*.*", "NOPRIV")
        expect(vm, "COPY SD0:[HOME.ADMIN]DUTY.TXT X.TXT", "NOPRIV")
        expect(vm, "COPY NOTICE.TXT SD0:[HOME.ADMIN]X.TXT", "NOPRIV")
        expect(vm, "AUTHORIZE LIST", "NOPRIV")
        expect(vm, "SET HALT", "NOPRIV")
        expect(vm, "DEVICE DOOR1 OPEN", "NOPRIV")
        _, _, s = expect(vm, "DEVICE LIGHT1 ON", "NORMAL")
        assert any(l.startswith("LIGHT1") and "ON" in l for l in out(vm, s)), out(vm, s)
        expect(vm, "DEVICE LIGHT1 OPEN", "IVACT")
        expect(vm, "DEVICE NOSUCH", "NODEV")
        _, _, s = expect(vm, "TYPE SYS$HELP:TYPE.HLP", "NORMAL")
        assert "TYPE file-spec" in out(vm, s)
        expect(vm, "LOGOUT", "NORMAL")
        wait_marker(vm, "session state=end user=GUEST reason=user", 1)
        login(vm, "ADMIN")
        expect(vm, "DELETE SD0:[000000]SYSUAF.DAT", "PROTECT", "%UOS-E-PROTECT")
        res, end, s = expect(vm, "DELETE SD0:[000000]*.*", "DELETED")
        names = [n for n, _ in files(vm, s, "delete")]
        assert not any("SYSUAF" in n for n in names), names
        assert int(end["skipped"]) >= 1, end
        _, _, s = expect(vm, "DIR SD0:[000000]", "NORMAL")
        assert "SYSUAF.DAT" in out(vm, s), out(vm, s)


def test_read_only_volume(board, hooks_image, need_mtools, tmp_path):
    """BOOT: is a read-only volume: every change is WRITLCK, reads work. QEMU takes
    no read-only SD card: the read-only card is covered on the host, test_vfs_card."""
    sd = shell_card(tmp_path, {"/ROBCO/README.TXT": b"BOOT VOLUME\n"})
    with alone(hooks_image, board, sd) as vm:
        wait_ready(vm)
        login(vm, "ADMIN")
        _, _, s = expect(vm, "TYPE BOOT:[000000]README.TXT", "NORMAL")
        assert "BOOT VOLUME" in out(vm, s)
        expect(vm, "COPY NOTICE.TXT BOOT:[000000]X.TXT", "WRITLCK", "%UOS-E-WRITLCK")
        expect(vm, "DELETE BOOT:[000000]README.TXT", "WRITLCK")
        expect(vm, "CREATE BOOT:[NEW] /DIRECTORY", "WRITLCK")
        expect(vm, "RENAME BOOT:[000000]README.TXT BOOT:[000000]R.TXT", "WRITLCK")


def test_copy_cancelled_with_ctrl_c(board, hooks_image, need_mtools, tmp_path):
    """A 1 MiB COPY runs in steps; Ctrl-C stops it: CANCEL, no target, no work file.
    A second command meanwhile is refused; nobody logged in is refused too."""
    with alone(hooks_image, board, shell_card(tmp_path, {"/UOS/HOME/ADMIN/BIG.BIN": BIG})) as vm:
        wait_ready(vm)
        assert vm.script("sh run DIR", timeout=TIMEOUT).status == "ok"
        assert wait_marker(vm, "sh run=refused", 1)["rc"] == "-3"
        login(vm, "ADMIN")
        c = send_line(vm, "COPY BIG.BIN BIG2.BIN")
        wait_state(vm, state="running", verb="COPY")
        answer(vm, "sh run DIR")
        assert wait_marker(vm, "sh run=refused", 2)["rc"] == "-2"
        vm.send("\x03")
        res, end, _ = wait_done(vm, c)
        assert end["rc"] == "CANCEL" and res == "%UOS-I-CANCEL, operation cancelled", (res, end)
        assert int(end["steps"]) < 64, end
        expect(vm, "TYPE BIG2.BIN", "FNF")
        # The whole copy, measured.
        res, end, _ = expect(vm, "COPY BIG.BIN BIG3.BIN", "COPIED")
        assert int(end["steps"]) >= 64, end
        print(f"\ncopy board={board} steps={end['steps']} maxticks={end['maxticks']}")
    names = card_names(tmp_path / "sd.img", "UOS/HOME/ADMIN")
    assert "BIG2.BIN" not in names and "BIG3.BIN" in names, names
    assert not [n for n in names if n.startswith("~") or n.startswith("#")], names


def test_set_halt_forms(board, hooks_image, need_mtools, tmp_path):
    """/MAINTENANCE is refused and nothing stops; RESTART reboots; SET HALT halts."""
    with alone(hooks_image, board, shell_card(tmp_path)) as vm:
        wait_ready(vm)
        login(vm, "OPER")
        expect(vm, "SET HALT /MAINTENANCE", "NOMAINT", "%UOS-E-NOMAINT")
        expect(vm, "SET HALT RESTART /MAINTENANCE", "NOMAINT")
        assert not events(vm, "reboot") and not events(vm, "halt")
        expect(vm, "SET HALT RESTART /NOMAINTENANCE", "NORMAL")
        vm.wait_for(REBOOTED, timeout=TIMEOUT)
        assert events(vm, "reboot")[0]["source"] == "shell"
    with alone(hooks_image, board, shell_card(tmp_path, name="sd2.img")) as vm:
        wait_ready(vm)
        login(vm, "OPER")
        answer(vm, "sh run " + quote("SET HALT"))
        vm.wait_for(IDLE, timeout=TIMEOUT)
        one(vm, "halt")
        end = [m for m in events(vm, "cmd") if m.get("state") == "end"][-1]
        assert (end["verb"], end["rc"]) == ("SET_HALT", "NORMAL"), end


def test_typed_line_continuation_and_missing_parameter(board, hooks_image, need_mtools,
                                                       tmp_path):
    """Lines typed through the real line editor: a command, a line ending in '-'
    with the "_$" prompt, a missing parameter with the "_File:" prompt, Ctrl-C at a
    prompt; then LOGOUT typed."""
    with alone(hooks_image, board, shell_card(tmp_path)) as vm:
        wait_ready(vm)
        login(vm, "ADMIN")
        e = n_marks(vm, "cmd state=end")
        answer(vm, "type SHOW -")
        wait_state(vm, state="prompt", kind="contin")
        dump_until(vm, lambda r: any(row.startswith("_$") for row in r))
        answer(vm, "type DEFAULT")
        end = wait_marker(vm, "cmd state=end", e + 1)
        assert (end["verb"], end["rc"]) == ("SHOW_DEFAULT", "NORMAL"), end
        wait_state(vm, state="prompt", kind="cmd")
        answer(vm, "type TYPE")
        wait_state(vm, state="prompt", kind="param")
        dump_until(vm, lambda r: any(row.startswith("_File:") for row in r))
        answer(vm, "type NOTICE.TXT")
        end = wait_marker(vm, "cmd state=end", e + 2)
        assert (end["verb"], end["rc"]) == ("TYPE", "NORMAL"), end
        # An empty answer asks again; Ctrl-C drops the command.
        wait_state(vm, state="prompt", kind="cmd")
        answer(vm, "type SET")
        wait_state(vm, state="prompt", kind="param")
        dump_until(vm, lambda r: any(row.startswith("_What:") for row in r))
        vm.send("\x03")
        wait_state(vm, state="prompt", kind="cmd")
        assert n_marks(vm, "cmd state=end") == e + 2
        # The rows show the commands as typed.
        rows = dump_until(vm, lambda r: any(row.startswith("$ SHOW -") for row in r))
        assert any(row.startswith("_$ DEFAULT") for row in rows), rows
        answer(vm, "type LOGOUT")
        end = wait_marker(vm, "session state=end", 1)
        assert end["reason"] == "user", end
        assert answer(vm, "sess state")[0].split()[3] == "user=none"


def test_authorize_and_set_password(board, hooks_image, need_mtools, tmp_path):
    """AUTHORIZE ADD, LIST, MODIFY and REMOVE; a new user logs in with the password
    typed masked; SET PASSWORD with a wrong old one, a wrong verification, then a
    good change; no password ever reaches the serial lines."""
    new_pw, newer_pw = "Bobs-1st-Word", "Bobs-2nd-Word"
    with alone(hooks_image, board, shell_card(tmp_path)) as vm:
        wait_ready(vm)
        login(vm, "ADMIN")
        expect(vm, "AUTHORIZE ADD BOB /PASSWORD=X", "NOVALU")
        expect(vm, "AUTHORIZE ADD BAD-NAME", "IVNAME")
        expect(vm, "AUTHORIZE ADD LONGNAMEUSER", "IVHOME")
        expect(vm, "AUTHORIZE ADD GUEST", "EXISTS")
        c = send_line(vm, "AUTHORIZE ADD BOB /PRIVS=(OPER,READALL) /NOHACKABLE")
        reply(vm, "password", new_pw)
        reply(vm, "password", new_pw)
        res, end, _ = wait_done(vm, c)
        assert res == "%UAF-S-ADDED, user BOB added", (res, end)
        assert marks(vm, "sessdb")[-1]["users"] == "4"
        _, _, s = expect(vm, "AUTHORIZE LIST BOB", "NORMAL")
        assert any(re.match(r"BOB +BOB +OPER,READALL +NOHACK", l) for l in out(vm, s)), \
            out(vm, s)
        _, _, s = expect(vm, "AUTHORIZE LIST", "NORMAL")
        assert sum(1 for l in out(vm, s) if re.match(r"(ADMIN|OPER|GUEST|BOB) ", l)) == 4
        # A mismatched verification changes nothing.
        c = send_line(vm, "AUTHORIZE MODIFY BOB /PASSWORD")
        reply(vm, "password", "one-thing")
        reply(vm, "password", "another")
        res, _, _ = wait_done(vm, c)
        assert res.startswith("%SET-E-PWDNOTVER"), res
        expect(vm, "AUTHORIZE MODIFY BOB /DISABLED", "MODIFIED")
        _, _, s = expect(vm, "AUTHORIZE LIST BOB", "NORMAL")
        assert any("DISABLED" in l for l in out(vm, s))
        expect(vm, "AUTHORIZE MODIFY BOB /NODISABLED /HOME=BOBHOME", "MODIFIED")
        expect(vm, "AUTHORIZE MODIFY NOBODY /DISABLED", "NOUSER")
        expect(vm, "AUTHORIZE REMOVE ADMIN", "SELF")
        expect(vm, "LOGOUT", "NORMAL")
        # BOB logs in at the real login screen with the masked password.
        deadline = time.monotonic() + TIMEOUT
        while answer(vm, "sess state")[0].split()[1] != "state=user":
            assert time.monotonic() < deadline
            time.sleep(0.05)
        n = n_marks(vm, "login state=pass")
        answer(vm, "type BOB")
        wait_marker(vm, "login state=pass", n + 1)
        answer(vm, f"type {new_pw}")
        assert wait_marker(vm, "login user=BOB", 1)["result"] == "ok"
        wait_marker(vm, "session screen=shell", 2)
        wait_state(vm, state="prompt", kind="cmd")
        _, _, s = expect(vm, "SHOW DEFAULT", "NORMAL")
        assert "SD0:[HOME.BOBHOME]" in out(vm, s), out(vm, s)
        c = send_line(vm, "SET PASSWORD")
        reply(vm, "password", "wrong-old")
        reply(vm, "password", newer_pw)
        reply(vm, "password", newer_pw)
        res, _, _ = wait_done(vm, c)
        assert res.startswith("%SET-E-BADPWD"), res
        c = send_line(vm, "SET PASSWORD")
        reply(vm, "password", new_pw)
        reply(vm, "password", newer_pw)
        reply(vm, "password", newer_pw)
        res, _, _ = wait_done(vm, c)
        assert res == "%SET-S-PWDSET, password changed", res
        expect(vm, "AUTHORIZE LIST", "NOPRIV")
        expect(vm, "LOGOUT", "NORMAL")
        login(vm, "ADMIN")
        expect(vm, "AUTHORIZE REMOVE BOB", "REMOVED")
        assert marks(vm, "sessdb")[-1]["users"] == "3"
        log = vm.serial + vm.serial2
        for pw in (new_pw, newer_pw, "wrong-old", "one-thing", "another"):
            assert pw not in log, pw
    # The card holds a valid database of the three default users.
    names = card_names(tmp_path / "sd.img", "UOS")
    assert "SYSUAF.DAT" in names and not [n for n in names if n[0] in "~#"], names


def card_get(sd, path, tmp_path):
    """The bytes of one file on the card, through mtools."""
    dst = tmp_path / "get.bin"
    if dst.exists():
        dst.unlink()
    _mtools(sd, "mcopy", "-n", "::/" + path, os.fspath(dst))
    return dst.read_bytes()


OLD_LIS = b"OLD LISTING\r\nKEEP THESE BYTES\r\n"


def test_output_kept_on_failure_and_ctrl_c(board, hooks_image, need_mtools, tmp_path):
    """/OUTPUT=<existing file>: a failed DIRECTORY, a warning, no files and a Ctrl-C
    between two steps leave the file byte for byte; a success replaces it whole."""
    sd = shell_card(tmp_path, {"/UOS/HOME/ADMIN/LIST.LIS": OLD_LIS})
    with alone(hooks_image, board, sd) as vm:
        wait_ready(vm)
        login(vm, "ADMIN")
        expect(vm, "DIRECTORY [.NOPE] /OUTPUT=LIST.LIS", "DNF", "%UOS-E-DNF")
        expect(vm, "SHOW TIME /OUTPUT=LIST.LIS", "NOCLOCK")
        expect(vm, "DIR *.XYZ /OUTPUT=LIST.LIS", "NOFILES")
        # Ctrl-C while the listing waits between two steps.
        h = n_marks(vm, "sh held=1")
        answer(vm, "sh hold 1")
        c = send_line(vm, "DIRECTORY /FULL /OUTPUT=LIST.LIS")
        wait_marker(vm, "sh held=1", h + 1)
        vm.send("\x03")
        res, end, _ = wait_done(vm, c)
        assert end["rc"] == "CANCEL", (res, end)
        answer(vm, "sh hold 0")
        assert [m["verb"] for m in marks(vm, "cmd output=kept")] == \
            ["DIRECTORY", "SHOW_TIME", "DIRECTORY", "DIRECTORY"], marks(vm, "cmd output")
        assert not marks(vm, "cmd output=written")
        _, _, s = expect(vm, "TYPE LIST.LIS", "NORMAL")
        assert "KEEP THESE BYTES" in out(vm, s), out(vm, s)
        # A success writes the whole listing.
        expect(vm, "DIRECTORY /OUTPUT=NEW.LIS", "NORMAL")
        assert marks(vm, "cmd output=written")[-1]["verb"] == "DIRECTORY"
    assert card_get(sd, "UOS/HOME/ADMIN/LIST.LIS", tmp_path) == OLD_LIS
    new = card_get(sd, "UOS/HOME/ADMIN/NEW.LIS", tmp_path)
    assert b"Directory SD0:[HOME.ADMIN]" in new and b"Total of" in new, new
    names = card_names(sd, "UOS/HOME/ADMIN")
    assert not [n for n in names if n[0] in "~#"], names


def test_late_program_end_is_ignored(board, hooks_image, need_mtools, tmp_path):
    """A test program's run is cancelled, a second RUN starts, then the first run
    ends: its end is a stale callback and the second job keeps waiting for its own."""
    with alone(hooks_image, board, shell_card(tmp_path)) as vm:
        wait_ready(vm)
        login(vm, "ADMIN")
        answer(vm, "sh prog reg TERMINAL")
        assert wait_marker(vm, "sh prog=reg")["rc"] == "0"
        c = send_line(vm, "RUN TERMINAL")
        wait_marker(vm, "sh prog=start n=1")
        first = wait_state(vm, state="running", verb="RUN")["id"]
        vm.send("\x03")
        res, end, _ = wait_done(vm, c)
        assert end["rc"] == "CANCEL", (res, end)
        c = send_line(vm, "RUN TERMINAL")
        wait_marker(vm, "sh prog=start n=2")
        second = wait_state(vm, state="running", verb="RUN")["id"]
        assert second != first
        # The first run ends late, with a failure: ignored.
        answer(vm, "sh prog done 1 1")
        wait_marker(vm, "sh prog=done n=1")
        stale = wait_marker(vm, "sh stale=prog")
        assert (stale["id"], stale["cur"]) == (first, second), stale
        st = sh_state(vm)
        assert (st["state"], st["id"]) == ("running", second), st
        # The second run's own end completes it.
        answer(vm, "sh prog done 2 0")
        res, end, _ = wait_done(vm, c)
        assert (end["verb"], end["rc"]) == ("RUN", "NORMAL"), end
        assert res == "TEST PROGRAM RUN 2 ENDED", res
        # Its end again, with no job running: ignored too.
        answer(vm, "sh prog done 2 0")
        assert wait_marker(vm, "sh stale=prog", 2)["cur"] == "0"
        assert n_marks(vm, "sh run=done") == 2


def test_wildcards_count_refused_matches(board, hooks_image, need_mtools, tmp_path):
    """SYSUAF.DAT refused by the access table: every match refused is NOPRIV, some
    refused give the skipped count in the marker and a SKIPPED line, none is NOFILES."""
    sd = shell_card(tmp_path, {"/UOS/SPARE.TXT": b"SPARE\n", "/UOS/SPARE2.TXT": b"S2\n"})
    with alone(hooks_image, board, sd) as vm:
        wait_ready(vm)
        login(vm, "ADMIN")
        for line in ("DELETE SD0:[000000]SYSUAF.*", "RENAME SD0:[000000]SYSUAF.* X.*",
                     "DELETE SD0:[000000]SY*.D%T"):
            _, end, _ = expect(vm, line, "NOPRIV", "%UOS-E-NOPRIV")
            assert end["skipped"] == "1", (line, end)
        res, end, s = expect(vm, "DELETE SD0:[000000]S*.* /LOG", "DELETED")
        assert res == "%DELETE-S-DELETED, 2 files deleted", res
        assert (end["files"], end["skipped"]) == ("2", "1"), end
        assert "%UOS-I-SKIPPED, 1 file skipped: not enough privilege" in out(vm, s), out(vm, s)
        assert sorted(n for n, _ in files(vm, s, "delete")) == \
            ["SD0:[000000]SPARE.TXT;1", "SD0:[000000]SPARE2.TXT;1"], files(vm, s)
        _, end, _ = expect(vm, "DELETE SD0:[000000]NOPE*.*", "NOFILES", "%UOS-W-NOFILES")
        assert end["skipped"] == "0", end
    assert "SYSUAF.DAT" in card_names(sd, "UOS")


LONG_LINE = b"0123456789" * ((1 << 20) // 10 + 1)
ROW = re.compile(r"^[0-9]{80}\r?$", re.M)       # a full row: UOS_LINE_MAX + 1


def long_rows(vm, start):
    return len(ROW.findall(vm.serial[start:]))


def test_type_long_line_in_bounded_steps(board, hooks_image, need_mtools, tmp_path):
    """TYPE of a 1 MiB single-line file: rows come 8 per step, the tick runs during
    it, the longest step stays in its bound, and Ctrl-C stops it."""
    data = LONG_LINE[:(1 << 20)]
    with alone(hooks_image, board, shell_card(tmp_path, {"/UOS/HOME/ADMIN/LONG.TXT": data})) \
            as vm:
        wait_ready(vm)
        login(vm, "ADMIN")
        c = send_line(vm, "TYPE LONG.TXT")
        wait_state(vm, state="running", verb="TYPE")
        t0 = int(answer(vm, "uptime")[0].split()[2].split("=")[1])
        deadline = time.monotonic() + TIMEOUT
        while long_rows(vm, c[2]) < 40:
            assert time.monotonic() < deadline, vm.serial[-2000:]
            time.sleep(0.05)
        t1 = int(answer(vm, "uptime")[0].split()[2].split("=")[1])
        assert t1 > t0, (t0, t1)
        assert sh_state(vm)["state"] == "running"
        vm.send("\x03")
        res, end, _ = wait_done(vm, c)
        rows = long_rows(vm, c[2])
        print(f"\ntype board={board} rows={rows} steps={end['steps']} "
              f"maxticks={end['maxticks']}")
        assert end["rc"] == "CANCEL", (res, end)
        assert rows < len(data) // 80, rows
        # Eight rows a step: the read step prints none.
        assert rows <= 8 * (int(end["steps"]) - 1), (rows, end)
        assert int(end["maxticks"]) <= MAX_STEP_TICKS, end
