"""The panic path, through the test-only maintenance commands, and proof that the
shipped image has none of them."""

import os
import re
import subprocess

from uos import (IDLE, TIMEOUT, alone, answer, dump, events, one, order, state, uos_card,
                 wait_ready)
from harness.env import minimal_env


def nm(image):
    elf = os.path.splitext(image)[0] + ".elf"
    out = subprocess.run(["arm-none-eabi-nm", elf], check=True, capture_output=True, text=True,
                         env=minimal_env())
    return {line.split()[-1] for line in out.stdout.splitlines() if line.strip()}


HOOKS = ("uos_test_fault", "uos_test_panic", "uos_test_tick_fail", "uos_test_early_fault")


def test_shipped_image_has_no_test_hooks(uos_image, hooks_image, tick_fail_image,
                                         early_fault_image):
    shipped = nm(uos_image)
    assert not shipped & set(HOOKS), shipped & set(HOOKS)
    # The same check sees each hook when it is built in.
    hooks = nm(hooks_image)
    assert "uos_test_fault" in hooks and "uos_test_panic" in hooks
    assert "uos_test_tick_fail" in nm(tick_fail_image)
    assert "uos_test_early_fault" in nm(early_fault_image)
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


def reboots(vm):
    """`reboot` answers from the panic loop: the board boots a second time."""
    vm.send("reboot\n", port=1)
    vm.wait_for(re.compile(r"(@RC stage=uos ev=boot [^\n]*\n[\s\S]*){2}"), timeout=TIMEOUT)
    assert one(vm, "reboot")["source"] == "maint"


def test_tick_failure_keeps_the_port(board, tick_fail_image, need_mtools, tmp_path):
    """A failed tick panics before the input layer starts: the panic path starts the port."""
    with alone(tick_fail_image, board, uos_card(tmp_path)) as vm:
        vm.wait_for(IDLE, timeout=TIMEOUT)
        p = one(vm, "panic")
        assert (p["kind"], p["reason"]) == ("call", "TICK_START_FAILED"), p
        assert one(vm, "tick")["rc"] == "-1"
        port = one(vm, "port")
        assert (port["late"], port["script"]) == ("1", "0"), port
        assert not events(vm, "input")
        st = state(vm)
        assert (st["phase"], st["screen"], st["error"]) == ("panic", "none", "panic"), st
        assert "REASON: TICK START FAILED" in rows_text(vm)
        reboots(vm)


def test_earliest_fault_keeps_the_port(board, early_fault_image, need_mtools, tmp_path):
    """The exception hook is the first way into the panic path, before the mini UART
    and the screen: the port still answers."""
    with alone(early_fault_image, board, uos_card(tmp_path)) as vm:
        vm.wait_for(IDLE, timeout=TIMEOUT)
        assert order(vm)[:3] == ["boot", "handoff", "panic"], vm.serial
        assert one(vm, "panic")["kind"] == "undef"
        port = one(vm, "port")
        assert (port["late"], port["mini"], port["script"]) == ("1", "0", "0"), port
        assert state(vm)["phase"] == "panic"
        reply = vm.script("dump", timeout=TIMEOUT)
        assert reply.status == "err no screen", reply
        reboots(vm)


# The base commands of src/kernel/maint.c: in the table on the panic path
# even when uos_maint_start() never ran.
BASE_COMMANDS = ["dump", "help", "reboot", "screens", "state", "timers", "type"]


def help_names(vm):
    return [line.split()[0] for line in answer(vm, "help")]


def test_early_panics_have_the_registry(board, tick_fail_image, early_fault_image, need_mtools,
                                        tmp_path):
    """Both early panics come before uos_maint_start() and the services: the panic path
    fills the registry, so the base commands answer, and only those."""
    for image in (tick_fail_image, early_fault_image):
        with alone(image, board, uos_card(tmp_path)) as vm:
            vm.wait_for(IDLE, timeout=TIMEOUT)
            assert help_names(vm) == BASE_COMMANDS, vm.serial
            assert answer(vm, "timers")[0].startswith("timers count=0 "), vm.serial
            assert vm.script("type abc", timeout=TIMEOUT).status == "err not ready"
            assert vm.script("services", timeout=TIMEOUT).status == "err unknown command"
            assert not events(vm, "service") and not events(vm, "shutdown"), vm.serial
            reboots(vm)
