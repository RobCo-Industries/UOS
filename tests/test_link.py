"""The services together: DEV: pseudo-files from the file system to the
device service, screens of the console on the kernel's stack beside device
changes, and the shutdown order of every service."""

import re

from test_dev import ALL, CFG, boot_card, dev_list, marks, record, wait_ready
from test_vfs import entries, err, ok
from uos import (IDLE, SERVICES, TIMEOUT, alone, answer, card, dump, dump_until, events, fs,
                 key, one, uos_card, wait_marker)

# The keyboard covers the bottom 5 rows of the 30-row console, src/osk/osk.h.
OSK_FIRST = 25


def test_dev_files(board, uos_image, need_mtools, tmp_path):
    """`fs` acts as a user on DEV: names; the device service answers and
    applies each device's own privilege mask to a write."""
    with alone(uos_image, board, boot_card(tmp_path, ALL)) as vm:
        wait_ready(vm)
        answer(vm, "fs who - 0")                       # a guest: no privilege
        final = fs(vm, "cat DEV:DOOR1")[0]
        ok(final)
        want = dev_list(vm)["DOOR1"].replace(" ", "_")
        assert want.startswith(final["line"]) and final["line"].startswith("DOOR1"), \
            (final, want)
        final, items = fs(vm, "ls DEV:")
        names = ["DOOR1", "ARM", "LIGHT1", "TUR1", "VAULT", "PIR", "FAN", "GATE", "BOLT"]
        ok(final, count=len(names), total=len(names))
        assert sorted(e.split(".")[0] for e in entries(items)) == sorted(names), items
        err(fs(vm, "cat DEV:NOPE")[0], "nodev")

        before = len(record(vm))
        err(fs(vm, "write DEV:DOOR1 OPEN")[0], "nopriv")   # DOOR1 needs SYSTEM or OPER
        err(fs(vm, "write DEV:FAN ON")[0], "nopriv")
        assert len(record(vm)) == before
        ok(fs(vm, "write DEV:LIGHT1 OFF")[0])              # privs = 0: anyone
        assert marks(vm, "dev", name="LIGHT1", state="OFF", by="dev")

        answer(vm, "fs who - 2")                       # OPER
        ok(fs(vm, "write DEV:DOOR1 OPEN")[0])
        assert marks(vm, "dev", name="DOOR1", state="OPEN", by="dev")
        assert " OPEN " in dev_list(vm)["DOOR1"]
        ok(fs(vm, "cat DEV:DOOR1")[0])
        assert "OPEN" in fs(vm, "cat DEV:DOOR1")[0]["line"]
        err(fs(vm, "write DEV:DOOR1 FLY")[0], "badarg")
        err(fs(vm, "write DEV:PIR ON")[0], "readonly")
        assert len(record(vm)) == before + 2


def test_devices_file_through_vfs(board, uos_image, need_mtools, tmp_path):
    """The device service reads DEVICES.CFG with vfs_sys_read_all(): a
    folder and a file over 8192 bytes are bad files, and a /UOS tree with no
    file is a missing one. Each leaves the service up with no devices."""
    cases = [("isdir", uos_card(tmp_path, extra={CFG + "/": b""}, name="dir.img")),
             ("toobig", boot_card(tmp_path, b";" * 8193)),
             (None, card(tmp_path, {"/UOS/": b""}, name="bare.img"))]
    for why, sd in cases:
        with alone(uos_image, board, sd) as vm:
            wait_ready(vm)
            cfg = one(vm, "devcfg")
            want = ("bad", why, "0") if why else ("missing", None, "0")
            assert (cfg["result"], cfg.get("why"), cfg["loaded"]) == want, cfg
            assert answer(vm, "dev list") == ["dev count=0"]


READING = "line state=reading"


def test_read_over_pager_and_pager_over_read(board, uos_image, need_mtools, tmp_path):
    """con.h: one read and one pager at a time, each on the kernel's screen
    stack. A read pushed over the pager takes the keys until it ends, then
    the pager has them again. A pager pushed over a read does the same."""
    with alone(uos_image, board, uos_card(tmp_path)) as vm:
        wait_ready(vm)
        answer(vm, "page long")
        wait_marker(vm, "page top=0", 1)
        answer(vm, "readline - 20 Over:")
        wait_marker(vm, READING, 1)
        answer(vm, "text ab")
        dump_until(vm, lambda rows: "Over: ab" in rows)
        key(vm, "back")                       # backspace in the read, not quit
        key(vm, "select")
        m = wait_marker(vm, "line result=ok", 1)
        assert m["text"] == '"a"', m
        assert not marks(vm, "page", result="done")
        key(vm, "down")                       # the pager has the keys again
        wait_marker(vm, "page top=1", 1)
        key(vm, "back")
        wait_marker(vm, "page result=done", 1)

        answer(vm, "readline - 20 Under:")
        wait_marker(vm, READING, 2)
        answer(vm, "text xy")
        dump_until(vm, lambda rows: "Under: xy" in rows)   # the keys reached the read
        answer(vm, "page short")
        wait_marker(vm, "page top=0", 2)
        assert vm.script("page short", timeout=TIMEOUT).status == "err busy"
        assert vm.script("readline - 5 Again:", timeout=TIMEOUT).status == "err busy"
        key(vm, "back")                       # quits the pager, not a backspace
        wait_marker(vm, "page result=done", 2)
        answer(vm, "text z")
        key(vm, "select")
        m = wait_marker(vm, "line result=ok", 2)
        assert m["text"] == '"xyz"', m


def test_device_changes_during_a_read(board, hooks_image, need_mtools, tmp_path):
    """A pulse that ends and a sensor change while a line is read change
    nothing in the read: the row and the text stay as typed."""
    with alone(hooks_image, board, boot_card(tmp_path, ALL)) as vm:
        wait_ready(vm)
        answer(vm, "readline - 30 Name:")
        wait_marker(vm, READING, 1)
        row = int(dict(f.split("=", 1) for f in answer(vm, "osk state")[0].split()[1:])["line"])
        answer(vm, "text abc")
        dump_until(vm, lambda rows: rows[row] == "Name: abc")
        assert answer(vm, "dev set GATE PULSE")
        vm.wait_for(re.compile(r"@RC stage=uos ev=dev name=GATE state=CLOSED by=pulse\r?\n"),
                    timeout=TIMEOUT)
        level = "1" if " TRIPPED " in dev_list(vm)["PIR"] else "0"    # the other state
        answer(vm, f"dev sense PIR {level}")
        vm.wait_for(re.compile(r"@RC stage=uos ev=dev name=PIR state=\w+ by=sensor\r?\n"),
                    timeout=TIMEOUT)
        answer(vm, "text d")
        dump_until(vm, lambda rows: rows[row] == "Name: abcd")
        key(vm, "select")
        m = wait_marker(vm, "line result=ok", 1)
        assert (m["len"], m["text"]) == ("4", '"abcd"'), m


def test_panic_with_keyboard_open(board, hooks_image, need_mtools, tmp_path):
    """A panic while the on-screen keyboard is open: the panic screen
    replaces every row, the read never calls back, the console stops, and
    the port still answers."""
    with alone(hooks_image, board, uos_card(tmp_path)) as vm:
        wait_ready(vm)
        answer(vm, "readline - 30 Word:")
        wait_marker(vm, READING, 1)
        answer(vm, "osk open")
        wait_marker(vm, "osk state=open", 1)
        rows = dump_until(vm, lambda rows: rows[OSK_FIRST].split() == list("ABCDEFGHIJKLM"))
        keyboard = [r for r in rows[OSK_FIRST:] if r.strip()]
        answer(vm, "panic")
        vm.wait_for(IDLE, timeout=TIMEOUT)
        _, after = dump(vm)
        after = [r.rstrip() for r in after]
        assert after[0] == " *** UOS HALTED: KERNEL PANIC ***", after
        assert not any(r in after for r in keyboard), after
        assert not any("Word:" in r for r in after), after
        assert [m["name"] for m in events(vm, "shutdown")] == list(reversed(SERVICES))
        assert not events(vm, "line") or all("result" not in m for m in events(vm, "line"))
        assert vm.script("osk close", timeout=TIMEOUT).status == "err not ready"
        assert vm.script("readline - 5 X:", timeout=TIMEOUT).status == "err console down"
        assert answer(vm, "state")
