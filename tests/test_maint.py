"""SET HALT and the maintenance request, /ROBCO/MAINT.REQ (BOOT_PROTOCOL.md,
"Maintenance request"). UOS boots alone here: no boot agent deletes the file,
so a test reads the card image on the host after QEMU stops. The whole road,
request, agent, then maintenance image, is tested in MFBootAgent."""

import os
import re
import subprocess

from harness.env import minimal_env
from uos import IDLE, ROOT, TIMEOUT, _mtools, alone, card_names, events, one, wait_ready
from test_shell import REBOOTED, expect, login, out, shell_card, wait_marker

REQ = "/ROBCO/MAINT.REQ"
READY_3 = re.compile(r"(@RC stage=uos ev=ready [^\n]*\n[\s\S]*){3}")


def req_bytes(sd, tmp_path):
    """The request file's bytes on the card, or None when it is not there."""
    if "MAINT.REQ" not in card_names(sd, "ROBCO"):
        return None
    dst = tmp_path / "maint.req"
    _mtools(sd, "mcopy", "-o", "::" + REQ, str(dst))
    return dst.read_bytes()


def maint_marks(vm):
    return events(vm, "maint_req")


def test_restart_with_request(board, hooks_image, need_mtools, tmp_path):
    """OPER writes the request and the terminal restarts. A second request finds
    the file and leaves it. The file holds "UOS OPER" after both restarts."""
    sd = shell_card(tmp_path, {"/ROBCO/": b""})
    with alone(hooks_image, board, sd) as vm:
        wait_ready(vm)
        login(vm, "OPER")
        _, _, s = expect(vm, "SET HALT RESTART /MAINTENANCE", "NORMAL")
        assert "MAINTENANCE REQUESTED FOR THE NEXT START." in out(vm, s), out(vm, s)
        m = wait_marker(vm, "maint_req", 1)
        assert (m["op"], m["result"], m["rc"]) == ("write", "written", "ok"), m
        vm.wait_for(REBOOTED, timeout=TIMEOUT)
        assert events(vm, "reboot")[0]["source"] == "shell"
        # The marker comes before the restart.
        evs = [x["ev"] for x in vm.markers() if x.get("stage") == "uos"]
        assert evs.index("maint_req") < evs.index("reboot"), evs
        wait_ready(vm)
        login(vm, "OPER")
        expect(vm, "SET HALT RESTART /MAINTENANCE", "NORMAL")
        m = wait_marker(vm, "maint_req", 2)
        assert (m["op"], m["result"], m["rc"]) == ("write", "kept", "ok"), m
        vm.wait_for(READY_3, timeout=TIMEOUT)
    assert req_bytes(sd, tmp_path) == b"UOS OPER\r\n"


def test_without_oper_refused(board, hooks_image, need_mtools, tmp_path):
    """GUEST has no OPER: NOPRIV, no request, no restart."""
    sd = shell_card(tmp_path, {"/ROBCO/": b""})
    with alone(hooks_image, board, sd) as vm:
        wait_ready(vm)
        login(vm, "GUEST")
        expect(vm, "SET HALT RESTART /MAINTENANCE", "NOPRIV", "%UOS-E-NOPRIV")
        expect(vm, "SET HALT /MAINTENANCE", "NOPRIV")
        expect(vm, "SHOW DEFAULT", "NORMAL")
        assert not maint_marks(vm)
        assert not events(vm, "reboot") and not events(vm, "halt")
    assert req_bytes(sd, tmp_path) is None


def test_failed_write_keeps_the_terminal_up(board, hooks_image, need_mtools, tmp_path):
    """No /ROBCO folder, then a folder named MAINT.REQ: the write fails, the
    message says why, nothing halts or restarts, and the shell goes on."""
    with alone(hooks_image, board, shell_card(tmp_path)) as vm:
        wait_ready(vm)
        login(vm, "OPER")
        res, _, _ = expect(vm, "SET HALT RESTART /MAINTENANCE", "NOMAINT")
        assert res == ("%UOS-E-NOMAINT, maintenance request not written: directory not "
                       "found; the terminal stays up"), res
        m = wait_marker(vm, "maint_req", 1)
        assert (m["op"], m["result"], m["rc"]) == ("write", "failed", "dirnotfound"), m
        expect(vm, "SET HALT /MAINTENANCE", "NOMAINT")
        expect(vm, "SHOW DEFAULT", "NORMAL")
        assert not events(vm, "reboot") and not events(vm, "halt")
    sd = shell_card(tmp_path, {REQ + "/": b""}, name="sd2.img")
    with alone(hooks_image, board, sd) as vm:
        wait_ready(vm)
        login(vm, "OPER")
        expect(vm, "SET HALT RESTART /MAINTENANCE", "NOMAINT")
        assert wait_marker(vm, "maint_req", 1)["rc"] == "isdir"
        assert not events(vm, "reboot")


def test_halt_with_request(board, hooks_image, need_mtools, tmp_path):
    """SET HALT /MAINTENANCE writes the request, then halts: the next power-on
    opens maintenance."""
    sd = shell_card(tmp_path, {"/ROBCO/": b""})
    with alone(hooks_image, board, sd) as vm:
        wait_ready(vm)
        login(vm, "OPER")
        expect(vm, "SET HALT /MAINTENANCE", "NORMAL")
        vm.wait_for(IDLE, timeout=TIMEOUT)
        one(vm, "halt")
        m = one(vm, "maint_req")
        assert (m["op"], m["result"]) == ("write", "written"), m
        assert not events(vm, "reboot")
    assert req_bytes(sd, tmp_path) == b"UOS OPER\r\n"


def test_nomaintenance_removes_and_plain_halt_keeps(board, hooks_image, need_mtools, tmp_path):
    """SET HALT RESTART /NOMAINTENANCE removes an earlier request, then restarts.
    A plain SET HALT leaves the file alone."""
    sd = shell_card(tmp_path, {REQ: b"EARLIER\n"})
    with alone(hooks_image, board, sd) as vm:
        wait_ready(vm)
        login(vm, "OPER")
        expect(vm, "SET HALT RESTART /NOMAINTENANCE", "NORMAL")
        m = wait_marker(vm, "maint_req", 1)
        assert (m["op"], m["result"], m["rc"]) == ("clear", "removed", "ok"), m
        vm.wait_for(REBOOTED, timeout=TIMEOUT)
    assert req_bytes(sd, tmp_path) is None
    sd = shell_card(tmp_path, {REQ: b"EARLIER\n"}, name="sd2.img")
    with alone(hooks_image, board, sd) as vm:
        wait_ready(vm)
        login(vm, "OPER")
        expect(vm, "SET HALT", "NORMAL")
        vm.wait_for(IDLE, timeout=TIMEOUT)
        one(vm, "halt")
        assert not maint_marks(vm)
    assert req_bytes(sd, tmp_path) == b"EARLIER\n"


def test_sdimage_ships_the_default_users(build_dir, need_mtools, tmp_path):
    """`make sdimage` puts /UOS/SYSUAF.DEF on the card with the bytes of a fresh
    /UOS/SYSUAF.DAT, for the maintenance image's restore users tool."""
    subprocess.run(["make", "-s", "sdimage", f"BUILD={build_dir}"], cwd=ROOT, check=True,
                   capture_output=True, env=minimal_env())
    sd = os.path.join(build_dir, "sd.img")
    names = card_names(sd, "UOS")
    assert "SYSUAF.DEF" in names and "SYSUAF.DAT" in names, names
    got = {}
    for n in ("SYSUAF.DAT", "SYSUAF.DEF"):
        _mtools(sd, "mcopy", "-o", "::/UOS/" + n, str(tmp_path / n))
        got[n] = (tmp_path / n).read_bytes()
    with open(os.path.join(ROOT, "card", "UOS", "SYSUAF.DAT"), "rb") as f:
        fresh = f.read()
    assert got["SYSUAF.DEF"] == got["SYSUAF.DAT"] == fresh
