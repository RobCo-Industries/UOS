"""The event loop: keys reach the screen, timers fire on time, a cancelled timer never fires."""

import re
import time

from harness.boot import SCRIPT_PORT

from uos import TICK_HZ, TIMEOUT, alone, answer, dump, events, key, one, timers, uos_card, \
    wait_ready

# The probe on the first screen, src/kernel/first.c.
PROBE_PERIOD = 10
PROBE_COUNT = 10

# The design bound: a periodic timer fires on the first loop pass at or
# after its deadline, so the probe ends PROBE_COUNT periods after its
# start plus at most one tick of loop latency.
LOOP_LATENCY_TICKS = 1
# Host allowance: QEMU runs beside other work on the test host, so the
# guest loop can be late. Catch-up keeps the count exact; the end may slip.
HOST_ALLOWANCE_TICKS = 20
# Wall-clock allowance for the whole probe on a loaded host, in seconds.
HOST_ALLOWANCE_S = 10

# The first screen's banner, BANNER_TICKS in src/kernel/first.c. The design
# delivers its one-shot timer on the first loop pass at or after the
# deadline, however much input waits: time goes into the queue first.
BANNER_TICKS = 50
# More characters than the event queue holds, UOS_EVQ_LEN in uos_event.h.
EVQ_LEN = 32
FLOOD_LINES = 1500
# The longest `type`: the input queue, INPUT_QUEUE_LEN in RobCo-Core,
# holds the characters, the SELECT press and its release.
INPUT_QUEUE_LEN = 32
TYPE_MAX = INPUT_QUEUE_LEN - 2

INPUT_MARK = re.compile(r"@RC stage=uos ev=input [^\n]*\n")
PROBE_DONE = re.compile(r"@RC stage=uos ev=timer name=probe done=1 [^\n]*\n")
WITNESS = re.compile(r"@RC stage=uos ev=timer name=witness [^\n]*\n")


def test_key_reaches_the_screen(board, uos_image, need_mtools, tmp_path):
    with alone(uos_image, board, uos_card(tmp_path)) as vm:
        wait_ready(vm)
        key(vm, "down")
        vm.wait_for(re.compile(r"@RC stage=uos ev=key [^\n]*keys=1\r?\n"), timeout=TIMEOUT)
        k = one(vm, "key")
        assert (k["screen"], k["type"], k["name"]) == ("first", "key", "down"), k
        # Releases are not events: one key, one marker.
        answer(vm, "char Q")
        vm.wait_for(re.compile(r"ev=key [^\n]*keys=2\r?\n"), timeout=TIMEOUT)
        last = events(vm, "key")[-1]
        assert (last["type"], last["code"], last["name"]) == ("char", "81", "'Q'"), last
        _, rows = dump(vm)
        assert "KEYS: 2, LAST 'Q'" in [r.rstrip() for r in rows], rows


def test_type_sends_a_line(board, uos_image, need_mtools, tmp_path):
    with alone(uos_image, board, uos_card(tmp_path)) as vm:
        wait_ready(vm)
        answer(vm, "type AB C")
        vm.wait_for(re.compile(r"ev=key [^\n]*keys=5\r?\n"), timeout=TIMEOUT)
        names = [m["name"] for m in events(vm, "key")]
        assert names == ["'A'", "'B'", "0x20", "'C'", "select"], names


def test_keys_flood_during_the_banner(board, uos_image, need_mtools, tmp_path):
    """Bursts of keys bigger than the queue must not lose the banner timer."""
    with alone(uos_image, board, uos_card(tmp_path)) as vm:
        vm.wait_for(INPUT_MARK, timeout=TIMEOUT)
        # One write of FLOOD_LINES `text` commands, each one queue of
        # characters: the input layer refills as fast as the loop drains it.
        line = "text " + "x" * EVQ_LEN + "\n"
        vm.send(line * FLOOD_LINES, port=SCRIPT_PORT)
        wait_ready(vm)
        ready = one(vm, "ready")
        assert ready["screen"] == "first", ready
        banner = int(ready["banner"])
        assert BANNER_TICKS <= banner <= BANNER_TICKS + LOOP_LATENCY_TICKS + \
            HOST_ALLOWANCE_TICKS, ready


def type_line(vm, n):
    """`type` with n characters; returns the reply status."""
    text = "".join(chr(ord("A") + i % 26) for i in range(n))
    return text, vm.script(f"type {text}", timeout=TIMEOUT).status


def test_type_lengths(board, uos_image, need_mtools, tmp_path):
    with alone(uos_image, board, uos_card(tmp_path)) as vm:
        wait_ready(vm)
        total = 0
        for n in (1, 15, TYPE_MAX):
            text, status = type_line(vm, n)
            assert status == "ok", (n, status)
            total += n + 1
            vm.wait_for(re.compile(rf"ev=key [^\n]*keys={total}\r?\n"), timeout=TIMEOUT)
            names = [m["name"] for m in events(vm, "key")][-(n + 1):]
            assert names == [f"'{c}'" for c in text] + ["select"], (n, names)
        # One more than fits: refused whole, nothing typed.
        _, status = type_line(vm, TYPE_MAX + 1)
        assert status == "err full", status
        answer(vm, "char Q")
        vm.wait_for(re.compile(rf"ev=key [^\n]*keys={total + 1}\r?\n"), timeout=TIMEOUT)
        assert events(vm, "key")[-1]["name"] == "'Q'"


def test_serial_console_keys(board, uos_image, need_mtools, tmp_path):
    """The PL011 console backend: an ANSI arrow key and a letter."""
    with alone(uos_image, board, uos_card(tmp_path)) as vm:
        wait_ready(vm)
        vm.send("\x1b[Ax")
        vm.wait_for(re.compile(r"ev=key [^\n]*keys=2\r?\n"), timeout=TIMEOUT)
        names = [m["name"] for m in events(vm, "key")]
        assert names == ["up", "'x'"], names


def test_periodic_timer_count_and_bound(board, uos_image, need_mtools, tmp_path):
    with alone(uos_image, board, uos_card(tmp_path)) as vm:
        wait_ready(vm)
        t0 = time.monotonic()
        key(vm, "f1")
        vm.wait_for(PROBE_DONE, timeout=TIMEOUT)
        wall = time.monotonic() - t0
        fires = [m for m in events(vm, "timer") if m["name"] == "probe" and "n" in m]
        assert [int(m["n"]) for m in fires] == list(range(1, PROBE_COUNT + 1)), vm.serial
        # Never early: the n-th fire is at least n periods after the start.
        for m in fires:
            assert int(m["ticks"]) >= int(m["n"]) * PROBE_PERIOD, m
        done = [m for m in events(vm, "timer") if m.get("done") == "1"][0]
        ticks = int(done["ticks"])
        nominal = PROBE_COUNT * PROBE_PERIOD
        assert nominal <= ticks <= nominal + LOOP_LATENCY_TICKS + HOST_ALLOWANCE_TICKS, done
        assert wall < nominal / TICK_HZ + HOST_ALLOWANCE_S, wall
        # The probe cancels itself: nothing fires after the end.
        time.sleep(3 * PROBE_PERIOD / TICK_HZ)
        assert len([m for m in events(vm, "timer") if m["name"] == "probe"]) == PROBE_COUNT + 1


def test_cancelled_timer_never_fires(board, uos_image, need_mtools, tmp_path):
    with alone(uos_image, board, uos_card(tmp_path)) as vm:
        wait_ready(vm)
        key(vm, "f2")
        vm.wait_for(WITNESS, timeout=TIMEOUT)
        cancel = [m for m in events(vm, "timer") if m["name"] == "cancel"]
        assert len(cancel) == 1 and cancel[0]["rc"] == "0", vm.serial
        witness = [m for m in events(vm, "timer") if m["name"] == "witness"][0]
        assert witness["victim"] == "0", witness
        assert not [m for m in events(vm, "timer") if m["name"] == "victim"], vm.serial


def test_double_f2_is_one_probe(board, uos_image, need_mtools, tmp_path):
    """F2 again while the cancel probe runs is ignored: one probe, no false failure."""
    with alone(uos_image, board, uos_card(tmp_path)) as vm:
        wait_ready(vm)
        key(vm, "f2")
        key(vm, "f2")
        vm.wait_for(WITNESS, timeout=TIMEOUT)
        names = [m["name"] for m in events(vm, "timer")]
        assert names.count("cancel") == 1 and names.count("witness") == 1, vm.serial
        assert "victim" not in names, vm.serial
        assert [m for m in events(vm, "timer") if m["name"] == "cancel"][0]["rc"] == "0"
        assert len([m for m in events(vm, "key") if m["name"] == "f2"]) == 2, vm.serial


def test_timers_listing(board, uos_image, need_mtools, tmp_path):
    with alone(uos_image, board, uos_card(tmp_path)) as vm:
        wait_ready(vm)
        # The clock refresh of the first screen: one periodic timer of one second.
        rows = timers(vm)
        assert [r["period"] for r in rows] == [str(TICK_HZ)], rows
        assert 0 <= int(rows[0]["due"]) <= TICK_HZ


def test_unknown_command(board, uos_image, need_mtools, tmp_path):
    with alone(uos_image, board, uos_card(tmp_path)) as vm:
        wait_ready(vm)
        reply = vm.script("frobnicate", timeout=TIMEOUT)
        assert reply.status == "err unknown command", reply
        # The test hooks are not in the shipped image.
        reply = vm.script("fault undef", timeout=TIMEOUT)
        assert reply.status == "err unknown command", reply
        reply = vm.script("panic", timeout=TIMEOUT)
        assert reply.status == "err unknown command", reply
        assert not events(vm, "panic")
