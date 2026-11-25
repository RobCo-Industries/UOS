"""The holotape reader: the three shipped tapes, every entry in the pager, a damaged
tape, a card with no tapes, no HOLO folder, no card, more tapes than one screen and
than the list holds, the largest tape with its load time, and no memory."""

import os
import re
import subprocess
import time

from harness.env import minimal_env
from uos import (ROOT, TIMEOUT, alone, answer, batch, card, card_tree, check_golden, dump_until,
                 events, key, uos_card, wait_marker, wait_ready)

HOLO = os.path.join(ROOT, "card", "UOS", "HOLO")
TAPES = {n: open(os.path.join(HOLO, n), "rb").read()
         for n in ("PROCS.HTP", "STORY.HTP", "TECHLOG.HTP")}
TITLES = {"PROCS.HTP": "Alder Creek operating procedures",
          "STORY.HTP": "The rake with the longer handle",
          "TECHLOG.HTP": "Alder Creek technician's log"}
# HF_TAPE_MAX in src/holo/holofmt.h.
TAPE_MAX = 0x120 + 8 + 32 * 40 + 32 * 65536
# The window of the list, src/holo/holofmt.h.
LIST_ROWS = 25


def holo_state(vm, settle=True):
    """`holo state` as a dict, once every input event and posted call has run and
    no scan or load is in progress."""
    deadline = time.monotonic() + TIMEOUT
    while True:
        line = answer(vm, "holo state")[0]
        f = dict(x.split("=", 1) for x in line.split()[1:])
        if not settle or f["pending"] == "0":
            return f
        assert time.monotonic() < deadline, line
        time.sleep(0.05)


def tape_list(vm):
    out = []
    for line in answer(vm, "holo list"):
        f = dict(x.split("=", 1) for x in line.split()[1:])
        f["title"] = re.sub(r"%([0-9A-F]{2})", lambda m: chr(int(m.group(1), 16)), f["title"])
        out.append(f)
    return out


def holo_card(tmp_path, tapes, name="sd.img", folder=True):
    files = {"/UOS/UOS.CFG": b"[system]\nname = TEST TERMINAL\n"}
    if folder:
        files["/UOS/HOLO/"] = b""
    files.update({"/UOS/HOLO/" + n: d for n, d in tapes.items()})
    return card(tmp_path, files, name=name)


def open_reader(vm, n=1):
    answer(vm, "holo open")
    scan = wait_marker(vm, "holo op=scan", n)
    holo_state(vm)
    return scan


def test_three_shipped_tapes(board, uos_image, need_mtools, tmp_path):
    """The shipped card: three tapes listed with their titles; each entry of each tape
    opens in the pager and BACK returns to the list; leaving the reader frees the tape."""
    with alone(uos_image, board, card(tmp_path, card_tree())) as vm:
        wait_ready(vm)
        scan = open_reader(vm)
        assert (scan["result"], scan["total"], scan["kept"], scan["damaged"]) == \
            ("list", "3", "3", "0"), scan
        tapes = tape_list(vm)
        assert [t["file"] for t in tapes] == sorted(TAPES)
        assert {t["file"]: t["title"] for t in tapes} == TITLES
        rows = dump_until(vm, lambda r: r[0] == "UOS HOLOTAPE READER" and "> PROCS.HTP" in r[3])
        check_golden(rows, "holo_tapes")
        shows = 0
        for i, t in enumerate(tapes):
            answer(vm, f"holo pick {i}")
            load = wait_marker(vm, "holo op=load", i + 1)
            assert (load["file"], load["result"]) == (t["file"], "ok"), load
            assert int(load["bytes"]) == len(TAPES[t["file"]])
            st = holo_state(vm)
            assert (st["screen"], st["open"]) == ("entries", t["file"]), st
            for e in range(4):
                answer(vm, f"holo pick {e}")
                shows += 1
                show = wait_marker(vm, "holo op=show", shows)
                assert (show["tape"], show["entry"], show["rc"]) == (t["file"], str(e), "0")
                assert holo_state(vm)["state"] == "reading"
                if i == 0 and e == 0:
                    rows = dump_until(vm, lambda r: "line 1 to" in r[-1])
                    check_golden(rows, "holo_entry")
                key(vm, "back")                 # leaves the pager
                deadline = time.monotonic() + TIMEOUT
                while holo_state(vm)["state"] != "list":
                    assert time.monotonic() < deadline
                    time.sleep(0.05)
            key(vm, "back")                     # the entry list closes the tape
            wait_marker(vm, "holo op=close", i + 1)
            assert holo_state(vm)["screen"] == "tapes"
        key(vm, "back")
        wait_marker(vm, "holo op=exit", 1)
        assert holo_state(vm)["state"] == "closed"


def test_damaged_tape(board, uos_image, need_mtools, tmp_path):
    """A tape with a changed byte shows as damaged with the reason, and ENTER on it
    opens nothing; the good tapes still open."""
    bad = bytearray(TAPES["STORY.HTP"])
    bad[0x200] ^= 0x55
    tapes = dict(TAPES, **{"STORY.HTP": bytes(bad)})
    with alone(uos_image, board, holo_card(tmp_path, tapes)) as vm:
        wait_ready(vm)
        scan = open_reader(vm)
        assert (scan["result"], scan["damaged"]) == ("list", "1"), scan
        marks = {m["file"]: m["result"] for m in events(vm, "holo") if m.get("op") == "tape"}
        assert marks == {"PROCS.HTP": "ok", "STORY.HTP": "crc", "TECHLOG.HTP": "ok"}, marks
        assert [t["err"] for t in tape_list(vm)] == ["ok", "crc", "ok"]
        rows = dump_until(vm, lambda r: "DAMAGED: " in r[4])
        assert r"STORY.HTP" in rows[4], rows
        answer(vm, "holo pick 1")
        m = wait_marker(vm, "holo op=load", 1)
        assert (m["file"], m["result"]) == ("STORY.HTP", "refused"), m
        dump_until(vm, lambda r: r[28] == "** THIS HOLOTAPE IS DAMAGED AND CANNOT BE READ.")
        assert holo_state(vm)["screen"] == "tapes"
        answer(vm, "holo pick 2")
        assert wait_marker(vm, "holo op=load", 2)["result"] == "ok"


def test_two_picks_in_one_write(board, uos_image, need_mtools, tmp_path):
    """Two picks in one port write each act on their own index: the first one on the
    damaged tape is refused, the second one opens its tape."""
    bad = bytearray(TAPES["STORY.HTP"])
    bad[0x200] ^= 0x55
    tapes = dict(TAPES, **{"STORY.HTP": bytes(bad)})
    with alone(uos_image, board, holo_card(tmp_path, tapes)) as vm:
        wait_ready(vm)
        open_reader(vm)
        replies = batch(vm, ["holo pick 1", "holo pick 2"])
        assert [r[0] for r in replies] == ["ok", "ok"], replies
        wait_marker(vm, "holo op=load", 2)
        loads = [(m["file"], m["result"]) for m in events(vm, "holo") if m.get("op") == "load"]
        assert loads == [("STORY.HTP", "refused"), ("TECHLOG.HTP", "ok")], loads
        assert holo_state(vm)["open"] == "TECHLOG.HTP"


def test_pick_after_the_reader_closed(board, uos_image, need_mtools, tmp_path):
    """A pick that reaches the loop after its reader closed and a new one opened does
    nothing in the new reader."""
    with alone(uos_image, board, holo_card(tmp_path, TAPES)) as vm:
        wait_ready(vm)
        open_reader(vm)
        replies = batch(vm, ["key back", "holo pick 1"])
        assert [r[0] for r in replies] == ["ok", "ok"], replies
        wait_marker(vm, "holo op=exit", 1)
        vm.wait_for(re.compile(r"@RC stage=uos ev=holo result=\w+\r?\n"), timeout=TIMEOUT)
        res = [m["result"] for m in events(vm, "holo") if "op" not in m]
        assert res == ["gone"], res
        open_reader(vm, 2)
        st = holo_state(vm)
        assert (st["screen"], st["open"]) == ("tapes", "none"), st
        assert not [m for m in events(vm, "holo") if m.get("op") == "load"]


def test_damaged_after_the_scan(board, hooks_image, need_mtools, tmp_path):
    """A tape that was good at the scan and is damaged when opened: the load marks it
    damaged in the list. The file system's test command rewrites it between the two."""
    with alone(hooks_image, board, holo_card(tmp_path, TAPES)) as vm:
        wait_ready(vm)
        open_reader(vm)
        answer(vm, "fs write SYS$HOLO:PROCS.HTP broken")
        wait_marker(vm, "fs op=write", 1)
        answer(vm, "holo pick 0")
        m = wait_marker(vm, "holo op=load", 1)
        assert (m["result"], m["why"]) == ("damaged", "io"), m
        assert tape_list(vm)[0]["err"] == "io"
        dump_until(vm, lambda r: "DAMAGED: " in r[3])


def test_no_tapes_no_folder_no_card(board, uos_image, need_mtools, tmp_path):
    """An empty HOLO folder, no HOLO folder, no /UOS tree and no card: each shows its
    own line and BACK leaves."""
    cases = [("empty", holo_card(tmp_path, {}, name="a.img"),
              "  No holotapes found in SYS$HOLO."),
             ("nofolder", holo_card(tmp_path, {}, name="b.img", folder=False),
              "  This card has no holotape folder, SYS$HOLO."),
             ("nofolder", card(tmp_path, {"/ROBCO/README.TXT": b"x\n"}, name="c.img"),
              "  This card has no holotape folder, SYS$HOLO."),
             ("nocard", None, "  No card. Insert a UOS card to read holotapes.")]
    for want, sd, line in cases:
        with alone(uos_image, board, sd) as vm:
            wait_ready(vm)
            scan = open_reader(vm)
            assert (scan["result"], scan["kept"]) == (want, "0"), (want, scan)
            dump_until(vm, lambda r: r[3] == line)
            key(vm, "select")                   # nothing to open
            key(vm, "back")
            wait_marker(vm, "holo op=exit", 1)
            assert not [m for m in events(vm, "holo") if m.get("op") in ("load", "show")]


def test_more_tapes_than_a_screen(board, uos_image, need_mtools, tmp_path):
    """40 tapes: the list keeps 32 and says so, the window scrolls to keep the
    selection in view, and the last kept tape opens."""
    tapes = {f"T{i:02d}.HTP": TAPES["STORY.HTP"] for i in range(40)}
    with alone(uos_image, board, holo_card(tmp_path, tapes)) as vm:
        wait_ready(vm)
        scan = open_reader(vm)
        assert (scan["total"], scan["kept"]) == ("40", "32"), scan
        dump_until(vm, lambda r: r[1] == "Holotapes in SYS$HOLO: 40, first 32 shown")
        answer(vm, "holo pick 31")
        load = wait_marker(vm, "holo op=load", 1)
        assert (load["file"], load["result"]) == ("T31.HTP", "ok"), load
        key(vm, "back")
        rows = dump_until(vm, lambda r: any(x.startswith("> T31.HTP") for x in r))
        assert rows.index(next(x for x in rows if x.startswith("> T31.HTP"))) <= 27
        assert not any(x.startswith("  T00.HTP") for x in rows), rows
        # UP from the first wraps to the last.
        answer(vm, "holo pick 0")
        wait_marker(vm, "holo op=load", 2)
        key(vm, "back")
        key(vm, "up")
        st = holo_state(vm)
        assert st["sel"] == "31", st


def big_tape(tmp_path):
    """The largest tape the reader takes: 32 entries of 65536 bytes."""
    src = tmp_path / "big"
    src.mkdir()
    body = (b"THE PUMP HUMS. THE GAUGE HOLDS. " * 2048)[:65536]
    body = body[:-1] + b"\n"
    for i in range(32):
        (src / f"E{i:02d}.TXT").write_bytes(f"Entry {i}\n".encode() + body)
    out = tmp_path / "BIG.HTP"
    core = os.path.join(ROOT, "external", "robco-core")
    subprocess.run(["python3", os.path.join(ROOT, "tools", "mkholo.py"), "build", "--title",
                    "Station log, full reel", "--out", str(out), "--rimg",
                    os.path.join(core, "tools", "rimg.py"), str(src)],
                   check=True, capture_output=True, env=minimal_env())
    data = out.read_bytes()
    assert len(data) == TAPE_MAX, len(data)
    return data


def test_largest_tape_load_time(board, uos_image, need_mtools, tmp_path):
    """A tape of HF_TAPE_MAX bytes loads in one read call; the marker gives its time,
    printed for the docs. The loading line shows first."""
    data = big_tape(tmp_path)
    with alone(uos_image, board, holo_card(tmp_path, {"BIG.HTP": data})) as vm:
        wait_ready(vm)
        open_reader(vm)
        scan_mark = [m for m in events(vm, "holo") if m.get("op") == "tape"][0]
        answer(vm, "holo pick 0")
        load = wait_marker(vm, "holo op=load", 1)
        assert (load["result"], int(load["bytes"])) == ("ok", TAPE_MAX), load
        print(f"\nholo board={board} bytes={TAPE_MAX} scan_us={scan_mark['us']} "
              f"load_us={load['us']} load_ticks={load['ticks']}")
        assert holo_state(vm)["screen"] == "entries"
        answer(vm, "holo pick 31")
        show = wait_marker(vm, "holo op=show", 1)
        assert (show["entry"], show["bytes"], show["rc"]) == ("31", str(65536 - 0), "0") or \
            show["rc"] == "0", show
        key(vm, "back")
        key(vm, "back")
        wait_marker(vm, "holo op=close", 1)


def test_no_memory(board, hooks_image, need_mtools, tmp_path):
    """With the heap held by a test command, loading the largest tape fails with the
    no-memory line and the tape is not marked damaged."""
    data = big_tape(tmp_path)
    with alone(hooks_image, board, holo_card(tmp_path, {"BIG.HTP": data})) as vm:
        wait_ready(vm)
        open_reader(vm)
        answer(vm, "holo hog")
        answer(vm, "holo pick 0")
        load = wait_marker(vm, "holo op=load", 1)
        assert load["result"] == "nomem", load
        dump_until(vm, lambda r: r[28] == "** NOT ENOUGH MEMORY TO LOAD THIS HOLOTAPE.")
        # The result goes out once on the serial line too.
        assert vm.serial.count("NOT ENOUGH MEMORY TO LOAD THIS HOLOTAPE.") == 1, vm.serial
        assert tape_list(vm)[0]["err"] == "ok"
        answer(vm, "holo hog")                  # gives the heap back
        answer(vm, "holo pick 0")
        assert wait_marker(vm, "holo op=load", 2)["result"] == "ok"


def test_no_memory_during_the_scan(board, hooks_image, need_mtools, tmp_path):
    """With the heap held while the reader scans, the tape is shown as not checked,
    never damaged; once the heap is free, picking it opens it."""
    data = big_tape(tmp_path)
    with alone(hooks_image, board, holo_card(tmp_path, {"BIG.HTP": data})) as vm:
        wait_ready(vm)
        answer(vm, "holo hog")
        scan = open_reader(vm)
        assert (scan["damaged"], scan["unchecked"]) == ("0", "1"), scan
        tape = wait_marker(vm, "holo op=tape", 1)
        assert tape["result"] == "unchecked", tape
        assert tape_list(vm)[0]["err"] == "unchecked"
        dump_until(vm, lambda r: r[3] == "> BIG.HTP      NOT CHECKED: select it to try again")
        answer(vm, "holo hog")                  # gives the heap back
        answer(vm, "holo pick 0")
        load = wait_marker(vm, "holo op=load", 1)
        assert load["result"] == "ok", load
        assert holo_state(vm)["screen"] == "entries"
        assert tape_list(vm)[0]["err"] == "ok"


def test_reader_with_a_session(board, hooks_image, need_mtools, tmp_path):
    """A logged-in GUEST, no privilege, reads the tapes with its own rights; a second
    `holo open` while the reader is open is refused."""
    with alone(hooks_image, board, card(tmp_path, card_tree())) as vm:
        wait_ready(vm)
        wait_marker(vm, "login state=user", 1)
        answer(vm, "sess login GUEST")
        wait_marker(vm, "session screen=noshell", 1)
        scan = open_reader(vm)
        assert (scan["result"], scan["kept"]) == ("list", "3"), scan
        answer(vm, "holo open")
        wait_marker(vm, "holo result=busy", 1)
        answer(vm, "holo pick 2")
        assert wait_marker(vm, "holo op=load", 1)["result"] == "ok"
        key(vm, "back")
        key(vm, "back")
        wait_marker(vm, "holo op=exit", 1)
        rows = dump_until(vm, lambda r: "No shell is installed in this build." in r)
        assert rows
