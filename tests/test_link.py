"""The services together: DEV: pseudo-files from the file system to the
device service, screens of the console on the kernel's stack beside device
changes, and the shutdown order of every service."""

import re

from test_dev import ALL, boot_card, dev_list, marks, record, wait_ready
from test_vfs import entries, err, ok
from uos import TIMEOUT, answer, alone, fs


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
