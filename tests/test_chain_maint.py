"""The maintenance road on the whole chain: three boots of one card in one VM,
every stage shipped. UOS asks for maintenance and restarts, the boot agent
opens its maintenance screen and starts the real maintenance image, a tool
runs, the image restarts, and the third boot is a normal one."""

import re
import time

from uos import TIMEOUT, after_chain, answer, card_names, wait_ready
from test_login import DEFAULT_USERS, login
from test_shell import expect, out, wait_marker, wait_state

BOOT = "@RC stage=bios ev=boot "
OPEN = re.compile(r"@RC stage=bootagent ev=maint_open [^\n]*\n")
MAINT_READY = re.compile(r"@RC stage=maint ev=ready\r?\n")
CHECKED = re.compile(r"@RC stage=maint ev=tool name=check_images [^\n]*\n")
MAINT_REBOOT = re.compile(r"@RC stage=maint ev=reboot [^\n]*\n")
# The agent's screen lists its actions from the top: "Start the maintenance
# image" is the fourth, as start_from_the_screen() in MFBootAgent's
# tests/test_maint_road.py picks it.
START_ITEM = 3
STATUS_ROW = 12
BIT1 = 1 << 1
BIT3 = 1 << 3


def boot_text(vm, n):
    """The serial text of the n-th boot, from its first stage's first marker."""
    parts = vm.serial.split(BOOT)
    assert len(parts) > n, f"boot {n} not started\n{vm.serial}"
    return BOOT + parts[n]


def marks(text, stage, ev):
    """The markers of one stage and event in `text`, as dicts."""
    out = []
    for line in text.replace("\r", "").splitlines():
        if line.startswith(f"@RC stage={stage} ev={ev}") and \
                line[len(f"@RC stage={stage} ev={ev}"):][:1] in ("", " "):
            out.append(dict(f.split("=", 1) for f in line[4:].split()))
    return out


def agent_settled(vm):
    """The agent's state line once it has taken every queued input event."""
    deadline = time.monotonic() + TIMEOUT
    while True:
        st = dict(f.split("=", 1) for f in answer(vm, "state")[0].split())
        if st["pending"] == "0":
            return st
        assert time.monotonic() < deadline, st
        time.sleep(0.05)


def test_whole_chain_maintenance(board, chain_card, chain_images, need_mtools):
    # The road needs the full card: the maintenance image, and every image
    # the scan is to check. The scan takes the names the loader takes.
    on_card = {n for n in card_names(chain_card, "ROBCO")
               if n.endswith((".SYS", ".BAK", ".IMG"))}
    assert {"MFBOOT.SYS", "UOS.SYS", "MAINT.SYS", "DIAG.SYS"} <= on_card, \
        f"the maintenance road needs a card built with MAINT_SYS, DIAG_SYS and BOOT_CFG: {on_card}"
    with after_chain(chain_images[0], board, chain_card) as vm:
        # First boot: ADMIN asks for maintenance and restarts.
        wait_ready(vm)
        login(vm, "ADMIN", DEFAULT_USERS["ADMIN"])
        wait_state(vm, state="prompt", kind="cmd")
        _, _, start = expect(vm, "SET HALT RESTART /MAINTENANCE", "NORMAL")
        assert "MAINTENANCE REQUESTED FOR THE NEXT START." in out(vm, start)
        m = wait_marker(vm, "maint_req", 1)
        assert (m["op"], m["result"], m["rc"]) == ("write", "written", "ok"), m
        assert wait_marker(vm, "reboot", 1)["source"] == "shell"

        # Second boot: the agent finds the request and opens its screen.
        vm.wait_for(OPEN, timeout=TIMEOUT)
        second = boot_text(vm, 2)
        req = marks(second, "bootagent", "maint_req")
        assert [(r["result"], r["rc"]) for r in req] == [("found", "0")], req
        assert [o["why"] for o in marks(second, "bootagent", "maint_open")] == ["request"]
        assert not marks(second, "bootagent", "countdown"), second
        assert not marks(second, "bootagent", "choose"), second
        for _ in range(START_ITEM):
            answer(vm, "key down")
            agent_settled(vm)
        answer(vm, "key select")

        vm.wait_for(MAINT_READY, timeout=TIMEOUT)
        second = boot_text(vm, 2)
        c = marks(second, "bootagent", "choose")
        assert [(x["name"], x["path"]) for x in c] == [("MAINT", "/ROBCO/MAINT.SYS")], c
        hand = marks(second, "maint", "handoff")
        assert len(hand) == 1 and hand[0]["trusted"] == "1", hand
        flags = int(hand[0]["flags"], 16)
        assert flags & (BIT1 | BIT3) == BIT1 | BIT3, hex(flags)
        rows = answer(vm, "dump")
        rows = [r[1:] for r in rows if r.startswith("|")]
        assert rows[0].startswith("TERMINAL MAINTENANCE"), rows
        assert rows[STATUS_ROW].rstrip() == "Started by a request. Choose a tool.", rows

        # The real image checks the real agent, UOS, MAINT.SYS and DIAG.SYS.
        answer(vm, "tool check_images")
        vm.wait_for(CHECKED, timeout=TIMEOUT)
        t = marks(boot_text(vm, 2), "maint", "tool")
        assert len(t) == 1 and (t[0]["name"], t[0]["result"], t[0]["bad"], t[0]["writes"]) == \
            ("check_images", "ok", "0", "0"), t
        scanned = {i["path"]: i["rc"] for i in marks(boot_text(vm, 2), "maint", "image")}
        assert scanned == {f"/ROBCO/{n}": "ok" for n in on_card}, scanned
        assert t[0]["images"] == str(len(on_card)), t
        answer(vm, "tool restart")
        vm.wait_for(MAINT_REBOOT, timeout=TIMEOUT)
        t = [x["name"] for x in marks(boot_text(vm, 2), "maint", "tool")]
        assert t == ["check_images", "restart"], t

        # Third boot: no request, the countdown, UOS by the timeout.
        # UOS starts in the first and third boots only: its second ready.
        wait_marker(vm, "ready", 2)
        third = boot_text(vm, 3)
        req = marks(third, "bootagent", "maint_req")
        assert [(r["result"], r["rc"]) for r in req] == [("none", "0")], req
        assert not marks(third, "bootagent", "maint_open"), third
        cd = marks(third, "bootagent", "countdown")
        assert cd and (cd[0]["state"], cd[0]["seconds"]) == ("start", "3"), cd
        c = marks(third, "bootagent", "choose")
        assert [(x["name"], x["via"]) for x in c] == [("UOS", "timeout")], c
        ready = marks(third, "uos", "ready")
        assert [r["screen"] for r in ready] == ["login"], ready
        login(vm, "GUEST", DEFAULT_USERS["GUEST"])
    assert "MAINT.REQ" not in card_names(chain_card, "ROBCO")
