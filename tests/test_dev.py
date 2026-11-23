"""The device service: DEVICES.CFG at boot, the pin record, `dev set` for every
action, privileges, pulses, sensors, bad files, expander pins, and the safe
state on reboot, panic and the watchdog. QEMU cannot show a GPIO level from
outside, so the tests read the service's pin record and its markers."""

import os
import re
import time

from uos import IDLE, ROOT, TICK_HZ, TIMEOUT, alone, answer, events, one, uos_card, wait_ready

CFG = "/UOS/DEVICES.CFG"

# Ticks a loaded test host may add to a pulse, the loop's 1 tick of latency
# aside. A pulse of N ms must end between N/10 ticks and that plus 1 plus
# this allowance.
HOST_ALLOWANCE_TICKS = 20
# Seconds a loaded host may add to the watchdog's 15 s.
WDOG_S = 15
HOST_ALLOWANCE_S = 15

READY_TWICE = re.compile(r"(@RC stage=uos ev=ready [^\n]*\n[\s\S]*){2}")

with open(os.path.join(ROOT, "card", "UOS", "DEVICES.CFG"), "rb") as f:
    SHIPPED = f.read()

# Five outputs of every type, a sensor, ARM second in the file.
ALL = b"""
[device DOOR1]
type = door
gpio = 12
label = Front door
[device ARM]
type = relay
gpio = 13
initial = on
label = Load supply
privs = 1
[device LIGHT1]
type = light
gpio = 25
active = low
initial = on
privs = 0
[device TUR1]
type = turret
gpio = 16
[device VAULT]
type = lock
gpio = 20
pulse_ms = 2000
[device PIR]
type = sensor
gpio = 21
active = low
[device FAN]
type = relay
gpio = 19
privs = 6
[device GATE]
type = door
gpio = 24
pulse_ms = 500
[device BOLT]
type = lock
gpio = 23
pulse_ms = 1000
"""

# (pin, level) of the boot: file order, the sensor skipped, ARM last.
ALL_BOOT = [(12, 0), (25, 0), (16, 0), (20, 0), (19, 0), (24, 0), (23, 0), (13, 1)]
# The safe state: ARM off first, then the others at initial, last first.
ALL_SAFE = [(13, 0), (23, 0), (24, 0), (19, 0), (20, 0), (16, 0), (25, 0), (12, 0)]


def boot_card(tmp_path, cfg=ALL):
    extra = {} if cfg is None else {CFG: cfg}
    return uos_card(tmp_path, extra=extra)


def record(vm):
    lines = answer(vm, "dev rec")
    head = dict(f.split("=", 1) for f in lines[0].split()[1:])
    rows = [dict(f.split("=", 1) for f in line.split()) for line in lines[1:]]
    assert int(head["count"]) == len(rows), lines
    return [(int(r["pin"]), int(r["level"]), int(r["tick"])) for r in rows]


def pins(rows):
    return [(p, lv) for p, lv, _ in rows]


def dev_list(vm):
    lines = answer(vm, "dev list")
    assert lines[0] == f"dev count={len(lines) - 2 if len(lines) > 1 else 0}", lines
    return {line.split()[0]: line for line in lines[2:]}


def dev_set(vm, name, action, privs=None):
    cmd = f"dev set {name} {action}" + ("" if privs is None else f" {privs}")
    return vm.script(cmd, timeout=TIMEOUT)


def marks(vm, ev, **match):
    return [m for m in events(vm, ev) if all(m.get(k) == v for k, v in match.items())]


def wait_mark(vm, text, count=1, timeout=TIMEOUT):
    vm.wait_for(re.compile(r"(@RC stage=uos ev=" + text + r"[^\n]*\n[\s\S]*){%d}" % count),
                timeout=timeout)


def test_shipped_file(board, uos_image, need_mtools, tmp_path):
    with alone(uos_image, board, boot_card(tmp_path, SHIPPED)) as vm:
        wait_ready(vm)
        cfg = one(vm, "devcfg")
        assert (cfg["result"], cfg["loaded"], cfg["skipped"], cfg["errors"], cfg["warnings"]) \
            == ("ok", "4", "0", "0", "0"), cfg
        assert one(vm, "devboot")["arm"] == "none"
        assert one(vm, "wdog")["result"] == "on"
        rows = dev_list(vm)
        assert list(rows) == ["DOOR1", "LIGHT1", "VAULT", "MOTION1"], rows
        assert " CLOSED " in rows["DOOR1"] and rows["DOOR1"].endswith("GPIO 12"), rows
        assert " LOCKED " in rows["VAULT"], rows
        # DOOR1 off, active high; LIGHT1 off, active low; VAULT off.
        assert pins(record(vm)) == [(12, 0), (25, 1), (20, 0)], vm.serial


def test_boot_record_arm_last(board, uos_image, need_mtools, tmp_path):
    with alone(uos_image, board, boot_card(tmp_path)) as vm:
        wait_ready(vm)
        assert pins(record(vm)) == ALL_BOOT, vm.serial
        b = one(vm, "devboot")
        assert (b["count"], b["arm"]) == ("9", "ARM"), b
        assert " ON " in dev_list(vm)["ARM"]


# Per device: (action, state after) in order, from the initial state.
ACTIONS = {
    "DOOR1": [("OPEN", "OPEN"), ("CLOSE", "CLOSED"), ("ON", "OPEN"), ("OFF", "CLOSED"),
              ("TOGGLE", "OPEN"), ("TOGGLE", "CLOSED")],
    "LIGHT1": [("ON", "ON"), ("OFF", "OFF"), ("TOGGLE", "ON"), ("TOGGLE", "OFF")],
    "TUR1": [("ACTIVATE", "ACTIVE"), ("STANDBY", "STANDBY"), ("ON", "ACTIVE"),
             ("OFF", "STANDBY"), ("TOGGLE", "ACTIVE"), ("TOGGLE", "STANDBY")],
    "FAN": [("ON", "ON"), ("OFF", "OFF"), ("TOGGLE", "ON"), ("TOGGLE", "OFF")],
    "VAULT": [("UNLOCK", "UNLOCKED"), ("LOCK", "LOCKED"), ("ON", "UNLOCKED"),
              ("OFF", "LOCKED"), ("TOGGLE", "UNLOCKED"), ("TOGGLE", "LOCKED"),
              ("PULSE", "UNLOCKED"), ("LOCK", "LOCKED")],
}
PIN = {"DOOR1": (12, 1), "LIGHT1": (25, 0), "TUR1": (16, 1), "FAN": (19, 1), "VAULT": (20, 1)}
BAD = {"DOOR1": ["LOCK", "PULSE", "NOPE"], "LIGHT1": ["OPEN", "PULSE"], "TUR1": ["UNLOCK"],
       "FAN": ["ACTIVATE"], "VAULT": ["OPEN"]}


def test_set_every_action(board, uos_image, need_mtools, tmp_path):
    with alone(uos_image, board, boot_card(tmp_path)) as vm:
        wait_ready(vm)
        for name, steps in ACTIONS.items():
            pin, on_level = PIN[name]
            for action, want in steps:
                reply = dev_set(vm, name.lower(), action.lower())
                assert reply.status == "ok", (name, action, reply)
                assert reply.lines == [f"dev name={name} state={want}"], reply.lines
                last = record(vm)[-1]
                on = want in ("OPEN", "ON", "ACTIVE", "UNLOCKED")
                assert last[:2] == (pin, on_level if on else 1 - on_level), (name, action, last)
                assert f" {want} " in dev_list(vm)[name]
            for action in BAD[name]:
                reply = dev_set(vm, name, action)
                assert reply.status.startswith("err %DEVICE-W-"), (name, action, reply.status)
        assert dev_set(vm, "PIR", "ON").status == "err %DEVICE-W-READONLY, device is read-only"
        assert dev_set(vm, "NOPE", "ON").status == "err %DEVICE-W-NOSUCHDEV, no such device"
        assert dev_set(vm, "LIGHT1", "PULSE").status == \
            "err %DEVICE-W-NOPULSE, device has no pulse length"
        assert vm.script("dev set X", timeout=TIMEOUT).status.startswith("err usage")
        assert vm.script("dev set DOOR1 ON 12z", timeout=TIMEOUT).status == "err bad privs"
        assert vm.script("dev", timeout=TIMEOUT).status.startswith("err usage")
        assert marks(vm, "dev", name="DOOR1", state="OPEN", by="maint")


def test_privileges(board, uos_image, need_mtools, tmp_path):
    nopriv = "err %DEVICE-W-NOPRIV, insufficient privilege"
    with alone(uos_image, board, boot_card(tmp_path)) as vm:
        wait_ready(vm)
        before = len(record(vm))
        assert dev_set(vm, "DOOR1", "OPEN", 0).status == nopriv          # a guest
        assert dev_set(vm, "DOOR1", "OPEN", 8).status == nopriv          # READALL only
        assert dev_set(vm, "FAN", "ON", 2).status == nopriv              # needs OPER+SECURITY
        assert dev_set(vm, "ARM", "OFF", 2).status == nopriv             # privs = 1
        assert len(record(vm)) == before
        assert dev_set(vm, "DOOR1", "OPEN", 2).status == "ok"            # OPER
        assert dev_set(vm, "FAN", "ON", 6).status == "ok"
        assert dev_set(vm, "FAN", "OFF", 1).status == "ok"               # SYSTEM
        assert dev_set(vm, "ARM", "OFF", 0x1).status == "ok"
        assert dev_set(vm, "LIGHT1", "ON", 0).status == "ok"             # privs = 0
        assert dev_set(vm, "VAULT", "LOCK").status == "ok"               # default SYSTEM


def pulse_span(vm, pin, start_index):
    """Ticks between the on write and the off write of `pin` after `start_index`."""
    rows = [r for r in record(vm)[start_index:] if r[0] == pin]
    assert len(rows) == 2 and rows[0][1] != rows[1][1], rows
    return rows[1][2] - rows[0][2]


def check_span(span, ms):
    ticks = ms * TICK_HZ // 1000
    assert ticks <= span <= ticks + 1 + HOST_ALLOWANCE_TICKS, (span, ms)


def test_pulse(board, uos_image, need_mtools, tmp_path):
    with alone(uos_image, board, boot_card(tmp_path)) as vm:
        wait_ready(vm)
        n = len(record(vm))
        assert dev_set(vm, "VAULT", "UNLOCK").lines == ["dev name=VAULT state=UNLOCKED"]
        wait_mark(vm, "dev name=VAULT state=LOCKED by=pulse")
        check_span(pulse_span(vm, 20, n), 2000)
        assert " LOCKED " in dev_list(vm)["VAULT"]


def test_two_pulses(board, uos_image, need_mtools, tmp_path):
    with alone(uos_image, board, boot_card(tmp_path)) as vm:
        wait_ready(vm)
        n = len(record(vm))
        assert dev_set(vm, "GATE", "PULSE").status == "ok"
        assert dev_set(vm, "BOLT", "UNLOCK").status == "ok"
        wait_mark(vm, "dev name=BOLT state=LOCKED by=pulse")
        assert marks(vm, "dev", name="GATE", state="CLOSED", by="pulse")
        check_span(pulse_span(vm, 24, n), 500)
        check_span(pulse_span(vm, 23, n), 1000)


def test_command_cancels_pulse(board, uos_image, need_mtools, tmp_path):
    with alone(uos_image, board, boot_card(tmp_path)) as vm:
        wait_ready(vm)
        assert dev_set(vm, "BOLT", "UNLOCK").status == "ok"
        assert dev_set(vm, "BOLT", "LOCK").status == "ok"
        n = len(record(vm))
        time.sleep(1.5 + HOST_ALLOWANCE_TICKS / TICK_HZ)
        assert len(record(vm)) == n, record(vm)
        assert not marks(vm, "dev", name="BOLT", by="pulse"), vm.serial
        # PULSE during a pulse restarts it: one end only.
        n = len(record(vm))
        assert dev_set(vm, "BOLT", "PULSE").status == "ok"
        assert dev_set(vm, "BOLT", "PULSE").status == "ok"
        wait_mark(vm, "dev name=BOLT state=LOCKED by=pulse")
        time.sleep(0.5)
        assert len(marks(vm, "dev", name="BOLT", by="pulse")) == 1
        assert [lv for p, lv, _ in record(vm)[n:] if p == 23] == [1, 1, 0]


def test_pulse_end_after_slow_loop(board, hooks_image, need_mtools, tmp_path):
    """The loop stalls 3 s inside a 1 s pulse: the end comes late, once."""
    with alone(hooks_image, board, boot_card(tmp_path)) as vm:
        wait_ready(vm)
        n = len(record(vm))
        assert dev_set(vm, "BOLT", "UNLOCK").status == "ok"
        answer(vm, "dev stall 300")
        wait_mark(vm, "dev name=BOLT state=LOCKED by=pulse")
        done = [m for m in events(vm, "devstall") if "done" in m]
        assert done, vm.serial
        rows = [r for r in record(vm)[n:] if r[0] == 23]
        assert [lv for _, lv, _ in rows] == [1, 0], rows
        assert rows[1][2] >= int(done[0]["done"]), (rows, done)
        assert rows[1][2] - rows[0][2] >= 100, rows
        assert len(marks(vm, "dev", name="BOLT", by="pulse")) == 1


def test_sensor_debounce(board, hooks_image, need_mtools, tmp_path):
    with alone(hooks_image, board, boot_card(tmp_path)) as vm:
        wait_ready(vm)
        start = dev_list(vm)["PIR"]
        # Active low: level 1 is CLEAR, 0 is TRIPPED.
        first = "1" if " TRIPPED " in start else "0"
        word = {"1": "CLEAR", "0": "TRIPPED"}
        answer(vm, f"dev sense PIR {first}")
        wait_mark(vm, f"dev name=PIR state={word[first]} by=sensor")
        other = "0" if first == "1" else "1"
        answer(vm, f"dev sense PIR {other}")
        wait_mark(vm, f"dev name=PIR state={word[other]} by=sensor")
        assert f" {word[other]} " in dev_list(vm)["PIR"]
        assert len(marks(vm, "dev", name="PIR", by="sensor")) == 2
        assert vm.script("dev sense DOOR1 1", timeout=TIMEOUT).status == "err not a sensor"
        assert vm.script("dev sense PIR 2", timeout=TIMEOUT).status.startswith("err usage")


BAD_FILE = b"""
[device RX]
type = light
gpio = 14
[device D1]
type = door
gpio = 12
[device D2]
type = door
gpio = 12
[device ODD]
type = toaster
gpio = 13
[device HOT]
type = relay
gpio = 4
[device BTN]
type = light
gpio = 26
"""


def test_bad_file(board, uos_image, need_mtools, tmp_path):
    with alone(uos_image, board, boot_card(tmp_path, BAD_FILE)) as vm:
        wait_ready(vm)
        cfg = one(vm, "devcfg")
        assert (cfg["result"], cfg["loaded"], cfg["skipped"], cfg["warnings"]) == \
            ("ok", "2", "4", "1"), cfg
        errs = [(m["name"], m["line"], m["why"]) for m in events(vm, "deverr")]
        assert errs == [("RX", "4", "PINUSED"), ("D2", "9", "PINSHARED"),
                        ("ODD", "12", "BADTYPE"), ("BTN", "19", "PINUSED")], errs
        warns = [(m["name"], m["line"], m["why"]) for m in events(vm, "devwarn")]
        assert warns == [("HOT", "15", "PULLUP")], warns
        assert list(dev_list(vm)) == ["D1", "HOT"]


EXP_FILE = b"""
[expander 0]
addr = 0x20
[device LAMP]
type = light
gpio = X0.3
label = Store lamp
[device DOOR1]
type = door
gpio = 12
"""


def test_expander(board, uos_image, need_mtools, tmp_path):
    msg = "err %DEVICE-E-NOEXPDRV, expander not available in this build"
    with alone(uos_image, board, boot_card(tmp_path, EXP_FILE)) as vm:
        wait_ready(vm)
        assert one(vm, "devcfg")["loaded"] == "2"
        fail = one(vm, "devfail")
        assert (fail["name"], fail["op"], fail["why"]) == ("LAMP", "config", "NOEXPDRV"), fail
        rows = dev_list(vm)
        assert rows["LAMP"].endswith("X0.3") and " OFF " in rows["LAMP"], rows
        assert pins(record(vm)) == [(12, 0)]
        for action in ("ON", "OFF", "TOGGLE"):
            assert dev_set(vm, "LAMP", action).status == msg
        assert " OFF " in dev_list(vm)["LAMP"]
        assert len(marks(vm, "devfail", name="LAMP", op="write")) == 3
        assert pins(record(vm)) == [(12, 0)]
        assert dev_set(vm, "DOOR1", "OPEN").status == "ok"


def test_no_devices(board, uos_image, need_mtools, tmp_path):
    cases = [("missing", boot_card(tmp_path, None)),
             ("bad", uos_card(tmp_path, extra={CFG + "/": b""}, name="dir.img"))]
    for result, sd in cases:
        with alone(uos_image, board, sd) as vm:
            wait_ready(vm)
            cfg = one(vm, "devcfg")
            assert (cfg["result"], cfg["loaded"]) == (result, "0"), cfg
            assert one(vm, "wdog")["result"] == "off"
            assert marks(vm, "service", name="devices", rc="0")
            assert answer(vm, "dev list") == ["dev count=0"]
            assert answer(vm, "dev rec") == ["rec count=0 total=0"]
            assert dev_set(vm, "DOOR1", "ON").status == \
                "err %DEVICE-E-NODEVICES, no devices are configured"
            assert answer(vm, "state")


def test_no_card(board, uos_image, tmp_path):
    with alone(uos_image, board, None) as vm:
        wait_ready(vm)
        assert one(vm, "devcfg")["result"] == "skipped"
        assert answer(vm, "dev list") == ["dev count=0"]


def safe_writes(vm):
    return [(int(m["pin"]), int(m["level"]), m["name"], m["rc"]) for m in events(vm, "devsafe")]


def test_reboot_drives_safe(board, uos_image, need_mtools, tmp_path):
    with alone(uos_image, board, boot_card(tmp_path)) as vm:
        wait_ready(vm)
        for name, action in (("DOOR1", "OPEN"), ("VAULT", "UNLOCK"), ("LIGHT1", "OFF"),
                             ("FAN", "ON")):
            assert dev_set(vm, name, action).status == "ok"
        vm.send("reboot\n", port=1)
        vm.wait_for(READY_TWICE, timeout=TIMEOUT)
        safe = safe_writes(vm)
        assert [(p, lv) for p, lv, _, _ in safe] == ALL_SAFE, vm.serial
        assert safe[0][2] == "ARM" and all(rc == "0" for *_, rc in safe)
        evs = [m["ev"] for m in vm.markers()]
        first_down = evs.index("devdown")
        assert evs.index("devsafe") < first_down < evs.index("reboot"), evs
        assert events(vm, "devdown")[0]["why"] == "reboot"
        assert len(events(vm, "devboot")) == 2
        assert pins(record(vm)) == ALL_BOOT


def test_panic_drives_safe(board, hooks_image, need_mtools, tmp_path):
    with alone(hooks_image, board, boot_card(tmp_path)) as vm:
        wait_ready(vm)
        assert dev_set(vm, "DOOR1", "OPEN").status == "ok"
        answer(vm, "panic")
        vm.wait_for(IDLE, timeout=TIMEOUT)
        assert [(p, lv) for p, lv, _, _ in safe_writes(vm)] == ALL_SAFE, vm.serial
        evs = [m["ev"] for m in vm.markers()]
        assert evs.index("panic") < evs.index("devsafe") < evs.index("devdown") < \
            evs.index("idle"), evs
        assert events(vm, "devdown")[0]["why"] == "panic"
        # The port still answers; the record ends with the safe writes.
        assert pins(record(vm))[-len(ALL_SAFE):] == ALL_SAFE
        assert vm.script("dev set DOOR1 OPEN", timeout=TIMEOUT).status == "err not ready"
        assert " CLOSED " in dev_list(vm)["DOOR1"]


def test_watchdog_resets_hung_loop(board, hooks_image, need_mtools, tmp_path):
    with alone(hooks_image, board, boot_card(tmp_path)) as vm:
        wait_ready(vm)
        assert dev_set(vm, "DOOR1", "OPEN").status == "ok"
        assert dev_set(vm, "ARM", "ON").status == "ok"
        t0 = time.monotonic()
        answer(vm, "dev hang")
        vm.wait_for(READY_TWICE, timeout=WDOG_S + HOST_ALLOWANCE_S + TIMEOUT)
        took = time.monotonic() - t0
        exp = [m for m in events(vm, "wdog") if m["result"] == "expired"]
        assert len(exp) == 1, vm.serial
        assert took <= WDOG_S + HOST_ALLOWANCE_S + 10, took
        hang = one(vm, "devhang")
        assert [(p, lv) for p, lv, _, _ in safe_writes(vm)] == ALL_SAFE, vm.serial
        evs = [m["ev"] for m in vm.markers()]
        assert evs.index("devhang") < evs.index("devsafe") < evs.index("devdown"), evs
        assert events(vm, "devdown")[0]["why"] == "wdog"
        assert len(events(vm, "boot")) == 2
        # The new boot's record starts from the initial levels.
        assert pins(record(vm)) == ALL_BOOT
        assert hang
