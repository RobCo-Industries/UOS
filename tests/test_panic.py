"""The panic path, through the test-only maintenance commands, and proof that the
shipped image has none of them."""

import os
import re
import subprocess

from uos import IDLE, TIMEOUT, alone, answer, dump, one, state, uos_card, wait_ready
from harness.env import minimal_env


def nm(image):
    elf = os.path.splitext(image)[0] + ".elf"
    out = subprocess.run(["arm-none-eabi-nm", elf], check=True, capture_output=True, text=True,
                         env=minimal_env())
    return {line.split()[-1] for line in out.stdout.splitlines() if line.strip()}


def test_shipped_image_has_no_test_hooks(uos_image, hooks_image):
    shipped = nm(uos_image)
    hooks = nm(hooks_image)
    assert "uos_test_fault" not in shipped and "uos_test_panic" not in shipped
    # The same check sees the hooks when they are built in.
    assert "uos_test_fault" in hooks and "uos_test_panic" in hooks
    assert "uos_panic" in shipped


def rows_text(vm):
    _, rows = dump(vm)
    return [r.rstrip() for r in rows]


def test_undefined_instruction(board, hooks_image, need_mtools, tmp_path):
    with alone(hooks_image, board, uos_card(tmp_path)) as vm:
        wait_ready(vm)
        answer(vm, "fault undef")
        vm.wait_for(IDLE, timeout=TIMEOUT)
        p = one(vm, "panic")
        assert (p["kind"], p["reason"]) == ("undef", "CPU_EXCEPTION_undef"), p
        assert int(p["pc"], 16) >= 0x00400000
        # The maintenance port still answers.
        st = state(vm)
        assert (st["phase"], st["screen"], st["error"]) == ("panic", "none", "panic"), st
        rows = rows_text(vm)
        assert rows[0] == " *** UOS HALTED: KERNEL PANIC ***", rows
        assert "REASON: CPU EXCEPTION undef" in rows, rows
        assert "EXCEPTION: undef" in rows, rows
        assert any(r.startswith("R0  ") for r in rows), rows
        assert any(r.startswith("SP  ") for r in rows), rows
        assert "THE SYSTEM HAS STOPPED. RESTART THE TERMINAL." in rows, rows
        # Keys go nowhere; the loop is stopped.
        answer(vm, "key select")
        assert state(vm)["phase"] == "panic"


def test_prefetch_abort(board, hooks_image, need_mtools, tmp_path):
    with alone(hooks_image, board, uos_card(tmp_path)) as vm:
        wait_ready(vm)
        answer(vm, "fault pabt")
        vm.wait_for(IDLE, timeout=TIMEOUT)
        assert one(vm, "panic")["kind"] == "pabt"
        assert state(vm)["phase"] == "panic"


def test_panic_call(board, hooks_image, need_mtools, tmp_path):
    with alone(hooks_image, board, uos_card(tmp_path)) as vm:
        wait_ready(vm)
        answer(vm, "panic")
        vm.wait_for(IDLE, timeout=TIMEOUT)
        p = one(vm, "panic")
        assert (p["kind"], p["reason"]) == ("call", "TEST_PANIC_FROM_THE_MAINTENANCE_PORT"), p
        assert "pc" not in p
        rows = rows_text(vm)
        assert "REASON: TEST PANIC FROM THE MAINTENANCE PORT" in rows, rows
        assert not any(r.startswith("R0  ") for r in rows), rows
        # The port answers commands in the panic loop; test commands are refused there.
        reply = vm.script("panic", timeout=TIMEOUT)
        assert reply.status == "err unknown command", reply
        assert len([m for m in vm.markers() if m.get("ev") == "panic"]) == 1


def test_reboot(board, uos_image, need_mtools, tmp_path):
    """`reboot` restarts the board through the watchdog: UOS boots a second time."""
    with alone(uos_image, board, uos_card(tmp_path)) as vm:
        wait_ready(vm)
        vm.send("reboot\n", port=1)
        vm.wait_for(re.compile(r"(@RC stage=uos ev=ready [^\n]*\n[\s\S]*){2}"), timeout=TIMEOUT)
        assert one(vm, "reboot")["source"] == "maint"
        assert [m["ev"] for m in vm.markers() if m["ev"] in ("boot", "reboot")] == \
            ["boot", "reboot", "boot"], vm.serial
