"""Services and the maintenance command registry: the service markers, `uptime`,
the shutdown order on reboot, halt and panic, `help`, quoting, and the hash bench."""

import re
import time

from uos import (IDLE, SERVICE_COMMANDS, SERVICES, TIMEOUT, TICK_HZ, alone, answer, events,
                 one, state, uos_card, wait_ready)

# Seconds a loaded test host may add between the host clock and the guest's.
HOST_ALLOWANCE_S = 10
# Bench bound: 2000 iterations of uaf_hash() are 4000 SHA-256 compressions.
# QEMU runs them in 6 to 12 ms on every board, about 1 tick (see
# docs/TESTING.md). The bound is 50 ticks, 0.5 s: about 40 times the measured
# figure, which leaves room for a loaded test host, yet a 100-times slowdown
# (over 1 s) or a hang breaks it.
BENCH_ITERATIONS = 2000
BENCH_BOUND_TICKS = 50

BENCH = re.compile(r"@RC stage=uos ev=bench [^\n]*\n")
REBOOTED = re.compile(r"(@RC stage=uos ev=ready [^\n]*\n[\s\S]*){2}")


def services(vm):
    return [(m["name"], m["rc"]) for m in events(vm, "service")]


def test_service_markers(board, uos_image, need_mtools, tmp_path):
    with alone(uos_image, board, uos_card(tmp_path)) as vm:
        wait_ready(vm)
        assert services(vm) == [(name, "0") for name in SERVICES], vm.serial
        lines = answer(vm, "services")
        assert lines == [f"service name={name} rc=0" for name in SERVICES], lines
        assert state(vm)["error"] == "none"


def test_uptime(board, uos_image, need_mtools, tmp_path):
    t0 = time.monotonic()
    with alone(uos_image, board, uos_card(tmp_path)) as vm:
        wait_ready(vm)
        first = dict(f.split("=", 1) for f in answer(vm, "uptime")[0].split()[1:])
        time.sleep(1.0)
        second = dict(f.split("=", 1) for f in answer(vm, "uptime")[0].split()[1:])
        host_s = time.monotonic() - t0
        for u in (first, second):
            assert int(u["s"]) == int(u["ticks"]) // TICK_HZ, u
            assert int(u["start"]) <= int(u["ticks"]), u
        assert first["start"] == second["start"]
        # The tick runs on: at least one tick per host second slept is a low
        # bar; it can never run ahead of the host clock plus an allowance.
        assert int(second["ticks"]) > int(first["ticks"]), (first, second)
        assert int(second["s"]) <= host_s + HOST_ALLOWANCE_S, (second, host_s)


def test_shutdown_on_reboot(board, uos_image, need_mtools, tmp_path):
    """`reboot` shuts every started service down, last first, then reboots."""
    with alone(uos_image, board, uos_card(tmp_path)) as vm:
        wait_ready(vm)
        vm.send("reboot\n", port=1)
        vm.wait_for(REBOOTED, timeout=TIMEOUT)
        evs = [m for m in vm.markers() if m["ev"] in ("service", "shutdown", "reboot")]
        n = len(SERVICES)
        first_boot = evs[:n + n + 1]
        started = [m["name"] for m in first_boot[:n]]
        down = [m for m in first_boot[n:2 * n]]
        assert started == SERVICES, vm.serial
        assert [m["ev"] for m in down] == ["shutdown"] * n, vm.serial
        assert [m["name"] for m in down] == list(reversed(started)), vm.serial
        assert all(m["why"] == "reboot" for m in down), vm.serial
        assert first_boot[2 * n]["ev"] == "reboot", vm.serial


def test_shutdown_on_halt(board, hooks_image, need_mtools, tmp_path):
    with alone(hooks_image, board, uos_card(tmp_path)) as vm:
        wait_ready(vm)
        answer(vm, "halt")
        vm.wait_for(IDLE, timeout=TIMEOUT)
        down = events(vm, "shutdown")
        assert [m["name"] for m in down] == list(reversed(SERVICES)), vm.serial
        assert all(m["why"] == "halt" for m in down), vm.serial
        one(vm, "halt")
        st = state(vm)
        assert (st["phase"], st["screen"]) == ("halt", "none"), st
        # The loop is gone: commands that post to it are refused.
        assert vm.script("halt", timeout=TIMEOUT).status == "err unknown command"
        assert answer(vm, "uptime")


def test_shutdown_on_panic(board, hooks_image, need_mtools, tmp_path):
    with alone(hooks_image, board, uos_card(tmp_path)) as vm:
        wait_ready(vm)
        answer(vm, "panic")
        vm.wait_for(IDLE, timeout=TIMEOUT)
        down = events(vm, "shutdown")
        assert [m["name"] for m in down] == list(reversed(SERVICES)), vm.serial
        assert all(m["why"] == "panic" for m in down), vm.serial
        evs = [m["ev"] for m in vm.markers()]
        assert evs.index("panic") < evs.index("shutdown") < evs.index("idle"), evs


def test_help_and_errors(board, uos_image, need_mtools, tmp_path):
    with alone(uos_image, board, uos_card(tmp_path)) as vm:
        wait_ready(vm)
        lines = answer(vm, "help")
        names = [line.split()[0] for line in lines]
        assert names == sorted(names), lines
        for name in ("help", "state", "dump", "type", "timers", "reboot", "services", "uptime"):
            assert name in names, lines
        # The shipped image has no test commands.
        for name in ("fault", "panic", "halt", "bench"):
            assert name not in names, lines
        assert vm.script("nosuch", timeout=TIMEOUT).status == "err unknown command"
        assert vm.script("state extra", timeout=TIMEOUT).status == "err unknown command"
        assert vm.script('type "open', timeout=TIMEOUT).status == "err bad quotes"


def test_type_keeps_quoted_spaces(board, uos_image, need_mtools, tmp_path):
    """A quoted word keeps its spaces: the first screen shows the characters it got."""
    with alone(uos_image, board, uos_card(tmp_path)) as vm:
        wait_ready(vm)
        before = len(events(vm, "key"))
        answer(vm, 'type "a  b"')
        vm.wait_for(re.compile(r"@RC stage=uos ev=key [^\n]*name=select[^\n]*\n"),
                    timeout=TIMEOUT)
        codes = [int(m["code"]) for m in events(vm, "key")[before:] if m["type"] == "char"]
        assert codes == [ord(c) for c in "a  b"], vm.serial


def test_bench_uaf(board, hooks_image, need_mtools, tmp_path):
    """Records the hash time per board. The bound only catches a hang or a
    100-times slowdown: QEMU's speed says little about a real Pi."""
    with alone(hooks_image, board, uos_card(tmp_path)) as vm:
        wait_ready(vm)
        assert vm.script("bench uaf 0", timeout=TIMEOUT).status == "err iterations 1 to 100000"
        assert vm.script("bench sha 1", timeout=TIMEOUT).status.startswith("err usage")
        answer(vm, f"bench uaf {BENCH_ITERATIONS}")
        vm.wait_for(BENCH, timeout=TIMEOUT)
        b = one(vm, "bench")
        print(f"bench board={board} mmu={one(vm, 'mmu')['result']} "
              f"iterations={b['iterations']} ticks={b['ticks']} us={b['us']}")
        assert (b["name"], b["iterations"], b["rc"]) == ("uaf", str(BENCH_ITERATIONS), "0"), b
        assert int(b["ticks"]) <= BENCH_BOUND_TICKS, b


def test_failing_service(board, hooks_image, need_mtools, tmp_path):
    """Test builds put a service whose init returns -5 first. The boot goes on, the
    next service starts, `state` reports it, and the failed one is never shut down."""
    with alone(hooks_image, board, uos_card(tmp_path)) as vm:
        wait_ready(vm)
        assert services(vm) == [("testfail", "-5")] + [(n, "0") for n in SERVICES], vm.serial
        assert answer(vm, "services")[0] == "service name=testfail rc=-5"
        assert state(vm)["error"] == "service"
        answer(vm, "halt")
        vm.wait_for(IDLE, timeout=TIMEOUT)
        assert [m["name"] for m in events(vm, "shutdown")] == list(reversed(SERVICES)), \
            vm.serial


def test_panic_after_services(board, hooks_image, need_mtools, tmp_path):
    """A fault after the services started: each started service gets one shutdown with
    why=panic, then the port answers every registered command, test ones included."""
    with alone(hooks_image, board, uos_card(tmp_path)) as vm:
        wait_ready(vm)
        answer(vm, "fault undef")
        vm.wait_for(IDLE, timeout=TIMEOUT)
        down = events(vm, "shutdown")
        assert [(m["name"], m["why"]) for m in down] == \
            [(n, "panic") for n in reversed(SERVICES)], vm.serial
        names = [line.split()[0] for line in answer(vm, "help")]
        assert names == sorted(["help", "state", "dump", "type", "timers", "reboot", "services",
                                "fault", "panic", "halt", "bench"] +
                               [c for n in SERVICES for c in SERVICE_COMMANDS[n]]), names
        assert state(vm)["phase"] == "panic"
        assert answer(vm, "uptime")
        assert answer(vm, "services")[0] == "service name=testfail rc=-5"
        # A second panic command is refused: no loop takes it, no second shutdown.
        assert vm.script("panic", timeout=TIMEOUT).status == "err unknown command"
        vm.send("reboot\n", port=1)
        vm.wait_for(REBOOTED, timeout=TIMEOUT)
        # The reboot from the panic loop shuts nothing down again: once per boot.
        assert [m["why"] for m in events(vm, "shutdown")] == ["panic"] * len(SERVICES), \
            vm.serial
        assert one(vm, "reboot")["source"] == "maint"
