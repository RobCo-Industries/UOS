"""The device service: DEVICES.CFG at boot, the pin record, `dev set` for every
action, privileges, pulses, sensors, bad files, expander pins, and the safe
state on reboot, panic and the watchdog. QEMU cannot show a GPIO level from
outside, so the tests read the service's pin record and its markers."""

import os
import re
import time

from uos import (IDLE, ROOT, SERVICES, TICK_HZ, TIMEOUT, alone, answer, dump_until, events, one,
                 uos_card)
from uos import wait_ready as wait_uos

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


# The service reports its init on the first tick after `ready`; its last
# init line is the watchdog's.
WDOG_LINE = re.compile(r"@RC stage=uos ev=wdog result=(on|off)[^\n]*\n")


def wait_ready(vm):
    wait_uos(vm)
    vm.wait_for(WDOG_LINE, timeout=TIMEOUT)


def boot_card(tmp_path, cfg=ALL):
    extra = {} if cfg is None else {CFG: cfg}
    return uos_card(tmp_path, extra=extra)


def record(vm):
    lines = answer(vm, "dev rec")
    head = dict(f.split("=", 1) for f in lines[0].split()[1:])
    rows = [dict(f.split("=", 1) for f in line.split()) for line in lines[1:]]
    assert int(head["count"]) == len(rows), lines
    return [(pin_of(r["pin"]), int(r["level"]), int(r["tick"])) for r in rows]


def pin_of(text):
    """12 for a Pi pin, "X0.3" for an expander pin."""
    return text if text.startswith("X") else int(text)


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
        # The shipped image has no `dev fail`.
        assert vm.script("dev fail DOOR1 1", timeout=TIMEOUT).status == \
            "err usage: dev list|set|rec|hwdog"
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
[device NONE]
"""


def test_bad_file(board, uos_image, need_mtools, tmp_path):
    with alone(uos_image, board, boot_card(tmp_path, BAD_FILE)) as vm:
        wait_ready(vm)
        cfg = one(vm, "devcfg")
        assert (cfg["result"], cfg["loaded"], cfg["skipped"], cfg["warnings"]) == \
            ("ok", "2", "5", "1"), cfg
        errs = [(m["name"], m["line"], m["why"]) for m in events(vm, "deverr")]
        # A section error carries the header's line, a key error the key's.
        assert errs == [("RX", "4", "PINUSED"), ("D2", "8", "PINSHARED"),
                        ("ODD", "12", "BADTYPE"), ("BTN", "19", "PINUSED"),
                        ("NONE", "20", "EMPTY")], errs
        warns = [(m["name"], m["line"], m["why"]) for m in events(vm, "devwarn")]
        assert warns == [("HOT", "14", "PULLUP")], warns
        assert list(dev_list(vm)) == ["D1", "HOT"]


# An unknown key, a key given twice, and an unknown key in an expander
# section. Key names are case-sensitive.
BAD_KEYS = b"""[device LIGHT1]
type = light
gpio = 25
actve = low
[device DOOR1]
type = door
gpio = 12
active = low
active = high
[device GATE]
type = door
gpio = 24
[expander 0]
addr = 0x20
speed = 100
[device LAMP]
type = light
gpio = X0.1
"""
CFGERR = "%DEVICE-W-CFGERR, DEVICES.CFG has 4 errors: the serial line names each"
DEV_NOTICE = "DEVICES: SOME DEVICES REFUSED"


def test_bad_keys_refuse_the_section(board, hooks_image, need_mtools, tmp_path):
    """Each bad section is refused with a deverr marker at the key's line, its pin
    is never driven, SHOW DEVICES ends with the CFGERR line and the login screen
    shows the devices notice."""
    from test_login import ROW_NOTICE, wait_user_prompt
    from test_shell import expect, login, out, shell_card

    with alone(hooks_image, board, shell_card(tmp_path, {CFG: BAD_KEYS})) as vm:
        wait_ready(vm)
        cfg = one(vm, "devcfg")
        assert (cfg["result"], cfg["loaded"], cfg["skipped"], cfg["unknown"], cfg["errors"]) == \
            ("ok", "1", "3", "2", "4"), cfg
        errs = [(m["name"], m["line"], m["why"]) for m in events(vm, "deverr")]
        assert errs == [("LIGHT1", "4", "BADKEY"), ("DOOR1", "9", "DUPKEY"),
                        ("X0", "15", "BADKEY"), ("LAMP", "16", "NOCHIP")], errs
        assert pins(record(vm)) == [(24, 0)], vm.serial
        assert list(dev_list(vm)) == ["GATE"]
        wait_user_prompt(vm)
        dump_until(vm, lambda r: r[ROW_NOTICE].strip() == DEV_NOTICE)
        login(vm, "ADMIN")
        _, _, s = expect(vm, "SHOW DEVICES", "NORMAL")
        # The rows, without the next prompt.
        o = [line for line in out(vm, s) if line.strip() and not line.strip().startswith("$")]
        assert not any(line.startswith("LIGHT1") for line in o), o
        assert any(line.startswith("GATE") for line in o), o
        assert o[-1] == CFGERR, o


# QEMU's pca9554 model on the BSC1 bus, as in RobCo-Core's test_gpiox.py.
# qom-get of pinN gives the configuration bit OR the output bit: "high" for
# an input, the output level for an output.
def pca9554(addr, ident="gx0"):
    return ["-device", f"pca9554,bus=i2c-bus.1,address={addr:#x},id={ident}"]


def chip_pins(vm, ident="gx0"):
    return sum((vm.qmp.execute("qom-get", path=f"/machine/peripheral/{ident}",
                               property=f"pin{p}") == "high") << p for p in range(8))


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
[device HATCH]
type = sensor
gpio = X0.5
active = low
"""

EXP_ARM = EXP_FILE + b"""
[device ARM]
type = relay
gpio = 13
initial = on
"""


def test_expander_switches(board, uos_image, need_mtools, tmp_path):
    """An expander output follows commands on the chip, the record names its
    chip, and an expander sensor reads the chip's input pin."""
    with alone(uos_image, board, boot_card(tmp_path, EXP_ARM), extra_args=pca9554(0x20),
               qtest=True) as vm:
        wait_ready(vm)
        att = one(vm, "devexp")
        assert (att["chip"], att["addr"], att["result"], att["rc"]) == \
            ("0", "0x20", "ok", "0"), att
        assert not events(vm, "devfail") and not events(vm, "devarm"), vm.serial
        # ARM on a Pi pin: the ARM rule holds, nothing is refused.
        assert not events(vm, "deverr") and one(vm, "devcfg")["errors"] == "0", vm.serial
        # LAMP off at boot, HATCH an input, ARM on last.
        assert pins(record(vm)) == [("X0.3", 0), (12, 0), (13, 1)], vm.serial
        assert chip_pins(vm) & 0x28 == 0x20, hex(chip_pins(vm))
        rows = dev_list(vm)
        assert rows["LAMP"].endswith("X0.3") and " OFF " in rows["LAMP"], rows
        assert rows["ARM"].endswith("GPIO 13"), rows
        assert answer(vm, "dev set LAMP ON") == ["dev name=LAMP state=ON"]
        assert chip_pins(vm) >> 3 & 1 == 1
        assert pins(record(vm))[-1] == ("X0.3", 1)
        assert " ON " in dev_list(vm)["LAMP"]
        assert answer(vm, "dev set LAMP OFF") == ["dev name=LAMP state=OFF"]
        assert chip_pins(vm) >> 3 & 1 == 0
        # The sensor: pull X0.5 low, active low, so it trips.
        assert " CLEAR " in dev_list(vm)["HATCH"]
        vm.qmp.set_irq_in("/machine/peripheral/gx0", 5, 0)
        wait_mark(vm, "dev name=HATCH state=TRIPPED by=sensor")
        vm.qmp.set_irq_in("/machine/peripheral/gx0", 5, 1)
        wait_mark(vm, "dev name=HATCH state=CLEAR by=sensor")


def test_expander_missing_chip(board, uos_image, need_mtools, tmp_path):
    """No chip answers: its devices are unreachable, ARM stays off, the boot
    goes on, and arming by hand says the outputs are unreachable."""
    msg = "err %DEVICE-E-EXPABSENT, expander chip did not answer at boot"
    with alone(uos_image, board, boot_card(tmp_path, EXP_ARM)) as vm:
        wait_ready(vm)
        att = one(vm, "devexp")
        assert (att["chip"], att["result"], att["rc"]) == ("0", "absent", "-32"), att
        assert one(vm, "devcfg")["loaded"] == "4"
        fails = [(m["name"], m["why"]) for m in marks(vm, "devfail", op="config")]
        assert fails == [("LAMP", "EXPABSENT"), ("HATCH", "EXPABSENT")], fails
        assert [m["name"] for m in marks(vm, "devfail", op="read")] == ["HATCH"]
        held = one(vm, "devarm")
        assert (held["name"], held["result"], held["why"], held["count"]) == \
            ("ARM", "held", "unreachable", "1"), held
        # ARM's pin is written off, never on.
        assert pins(record(vm)) == [(12, 0), (13, 0)], vm.serial
        rows = dev_list(vm)
        assert rows["LAMP"].endswith("X0.3 UNREACHABLE") and " OFF " in rows["LAMP"], rows
        assert " OFF " in rows["ARM"] and rows["ARM"].endswith("GPIO 13 UNREACH=1"), rows
        for action in ("ON", "OFF", "TOGGLE"):
            assert dev_set(vm, "LAMP", action).status == msg
        assert " OFF " in dev_list(vm)["LAMP"]
        assert len(marks(vm, "devfail", name="LAMP", op="write")) == 3
        assert dev_set(vm, "DOOR1", "OPEN").status == "ok"
        assert answer(vm, "dev set ARM ON") == ["dev name=ARM state=ON, 1 outputs unreachable"]
        # The answer and the marker travel on two ports. Wait for the marker.
        wait_mark(vm, "devarm name=ARM result=on")
        on = marks(vm, "devarm", result="on")
        assert len(on) == 1 and (on[0]["why"], on[0]["count"]) == ("unreachable", "1"), on


def test_expander_wrong_address(board, uos_image, need_mtools, tmp_path):
    """DEVICES.CFG names 0x20, the chip sits at 0x21: nothing answers at 0x20,
    ARM stays off, and the chip at 0x21 is never written."""
    with alone(uos_image, board, boot_card(tmp_path, EXP_ARM), extra_args=pca9554(0x21)) as vm:
        wait_ready(vm)
        att = one(vm, "devexp")
        assert (att["addr"], att["result"]) == ("0x20", "absent"), att
        assert one(vm, "devarm")["result"] == "held"
        assert dev_list(vm)["LAMP"].endswith("X0.3 UNREACHABLE")
        # Power-on state of the chip: every pin an input.
        assert chip_pins(vm) == 0xFF


def test_expander_safe_on_reboot_skipped_on_panic(board, hooks_image, need_mtools, tmp_path):
    """Reboot drives the expander output safe through the chip. Panic starts no
    I2C transfer: the pin keeps its level, ARM still goes off first."""
    with alone(hooks_image, board, boot_card(tmp_path, EXP_ARM), extra_args=pca9554(0x20)) as vm:
        wait_ready(vm)
        assert answer(vm, "dev set LAMP ON") == ["dev name=LAMP state=ON"]
        vm.send("reboot\n", port=1)
        vm.wait_for(READY_TWICE, timeout=TIMEOUT)
        vm.wait_for(re.compile(r"(@RC stage=uos ev=wdog result=on[^\n]*\n[\s\S]*){2}"),
                    timeout=TIMEOUT)
        safe = safe_writes(vm)
        assert [(p, lv, rc) for p, lv, _, rc in safe] == \
            [(13, 0, "0"), (12, 0, "0"), ("X0.3", 0, "0")], vm.serial
        assert chip_pins(vm) >> 3 & 1 == 0
        assert answer(vm, "dev set LAMP ON") == ["dev name=LAMP state=ON"]
        answer(vm, "panic")
        vm.wait_for(IDLE, timeout=TIMEOUT)
        down = [m for m in events(vm, "devsafe")][3:]
        assert [(m["pin"], m["rc"], m["tries"]) for m in down] == \
            [("13", "0", "1"), ("12", "0", "1"), ("X0.3", "-44", "0")], vm.serial
        assert events(vm, "devdown")[-1]["why"] == "panic"
        # The lamp's pin is still high, and the status says so.
        assert chip_pins(vm) >> 3 & 1 == 1
        assert " ON " in dev_list(vm)["LAMP"]


NOARM = "err %DEVICE-E-NOARM, expander output needs an ARM relay on a Pi pin off at reset"


def test_expander_output_without_arm(board, uos_image, need_mtools, tmp_path):
    """No ARM: the expander output is refused, never written, and says why.
    The expander sensor and the Pi pin device still work."""
    with alone(uos_image, board, boot_card(tmp_path, EXP_FILE), extra_args=pca9554(0x20),
               qtest=True) as vm:
        wait_ready(vm)
        cfg = one(vm, "devcfg")
        assert (cfg["loaded"], cfg["errors"]) == ("3", "1"), cfg
        err = one(vm, "deverr")
        assert (err["name"], err["line"], err["why"]) == ("LAMP", "4", "NOARM"), err
        assert one(vm, "devboot")["arm"] == "none"
        rows = dev_list(vm)
        assert rows["LAMP"].endswith("X0.3 REFUSED=NOARM") and " OFF " in rows["LAMP"], rows
        # Never configured: the chip's pin is still an input.
        assert all(p != "X0.3" for p, _ in pins(record(vm))), vm.serial
        assert chip_pins(vm) >> 3 & 1 == 1
        for action in ("ON", "OFF"):
            assert dev_set(vm, "LAMP", action).status == NOARM
        assert chip_pins(vm) >> 3 & 1 == 1
        assert answer(vm, "dev set DOOR1 OPEN") == ["dev name=DOOR1 state=OPEN"]
        assert pins(record(vm))[-1] == (12, 1)
        vm.qmp.set_irq_in("/machine/peripheral/gx0", 5, 0)
        wait_mark(vm, "dev name=HATCH state=TRIPPED by=sensor")


def test_expander_arm_on_expander(board, uos_image, need_mtools, tmp_path):
    """ARM on an expander pin cannot be cut without I2C: it is refused too."""
    cfg = EXP_FILE + b"[device ARM]\ntype = relay\ngpio = X0.1\n"
    with alone(uos_image, board, boot_card(tmp_path, cfg), extra_args=pca9554(0x20),
               qtest=True) as vm:
        wait_ready(vm)
        errs = [(m["name"], m["why"]) for m in events(vm, "deverr")]
        assert errs == [("LAMP", "NOARM"), ("ARM", "NOARM")], errs
        assert one(vm, "devboot")["arm"] == "none"
        rows = dev_list(vm)
        assert rows["ARM"].endswith("X0.1 REFUSED=NOARM"), rows
        assert dev_set(vm, "ARM", "ON").status == NOARM
        assert chip_pins(vm) & 0x0A == 0x0A
        assert answer(vm, "dev set DOOR1 OPEN") == ["dev name=DOOR1 state=OPEN"]


def test_expander_panic_then_reboot(board, hooks_image, need_mtools, tmp_path):
    """Panic with an expander output on: ARM goes off first on its Pi pin,
    the expander write is skipped. After a reboot the output is back at its
    initial level before ARM goes on."""
    with alone(hooks_image, board, boot_card(tmp_path, EXP_ARM), extra_args=pca9554(0x20),
               qtest=True) as vm:
        wait_ready(vm)
        assert answer(vm, "dev set LAMP ON") == ["dev name=LAMP state=ON"]
        assert chip_pins(vm) >> 3 & 1 == 1
        answer(vm, "panic")
        vm.wait_for(IDLE, timeout=TIMEOUT)
        safe = safe_writes(vm)
        assert [(p, lv, n, rc) for p, lv, n, rc in safe] == \
            [(13, 0, "ARM", "0"), (12, 0, "DOOR1", "0"), ("X0.3", 0, "LAMP", "-44")], vm.serial
        assert marks(vm, "devsafe", name="LAMP")[0]["tries"] == "0"
        # ARM's Pi pin was written off; the lamp keeps its level.
        assert pins(record(vm))[-2:] == [(13, 0), (12, 0)]
        assert " OFF " in dev_list(vm)["ARM"]
        assert chip_pins(vm) >> 3 & 1 == 1
        vm.send("reboot\n", port=1)
        vm.wait_for(READY_TWICE, timeout=TIMEOUT)
        vm.wait_for(re.compile(r"(@RC stage=uos ev=wdog result=on[^\n]*\n[\s\S]*){2}"),
                    timeout=TIMEOUT)
        # The new boot: LAMP at its initial level first, ARM on last.
        assert pins(record(vm)) == [("X0.3", 0), (12, 0), (13, 1)], vm.serial
        assert chip_pins(vm) >> 3 & 1 == 0
        assert len(events(vm, "devarm")) == 0


def test_hwdog_off_by_default(board, uos_image, need_mtools, tmp_path):
    """No [watchdog] section: the hardware watchdog never starts."""
    with alone(uos_image, board, boot_card(tmp_path)) as vm:
        wait_ready(vm)
        off = one(vm, "hwdog")
        assert (off["result"], off["why"]) == ("off", "setting"), off
        assert answer(vm, "dev hwdog") == \
            ["hwdog running=0 timeout_ms=0 remaining_ms=0 held=0"]


HWDOG_CFG = b"[system]\nname = TEST TERMINAL\n[watchdog]\nenabled = on\ntimeout_ms = 4000\n"
HWDOG_START_TWICE = re.compile(
    r"(@RC stage=uos ev=hwdog result=start timeout_ms=4000\r?\n[\s\S]*){2}")


def test_hwdog_on_resets_at_start(board, uos_image, need_mtools, tmp_path):
    """QEMU 10.1 does not count the PM watchdog: wdog_start() resets the board
    at once. With the setting on, UOS boots, starts its services, starts the
    watchdog on the first loop tick in the ready phase and boots again. The
    reset repeats for good, so the test stops at the second start and the
    harness kills QEMU: it never waits on the loop."""
    sd = uos_card(tmp_path, cfg=HWDOG_CFG, extra={CFG: ALL})
    with alone(uos_image, board, sd) as vm:
        vm.wait_for(HWDOG_START_TWICE, timeout=TIMEOUT)
        ms = vm.markers()
        evs = [m["ev"] for m in ms]
        first = evs.index("hwdog")
        again = evs.index("boot", first)
        # The start comes after every service is up, then a new boot.
        up = [(m.get("name"), m.get("rc")) for m in ms[:first] if m["ev"] == "service"]
        assert up == [(name, "0") for name in SERVICES], evs
        assert "service" not in evs[first:again], evs


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
    return [(pin_of(m["pin"]), int(m["level"]), m["name"], m["rc"]) for m in events(vm, "devsafe")]


def test_reboot_drives_safe(board, uos_image, need_mtools, tmp_path):
    with alone(uos_image, board, boot_card(tmp_path)) as vm:
        wait_ready(vm)
        for name, action in (("DOOR1", "OPEN"), ("VAULT", "UNLOCK"), ("LIGHT1", "OFF"),
                             ("FAN", "ON")):
            assert dev_set(vm, name, action).status == "ok"
        vm.send("reboot\n", port=1)
        vm.wait_for(READY_TWICE, timeout=TIMEOUT)
        vm.wait_for(re.compile(r"(@RC stage=uos ev=wdog result=on[^\n]*\n[\s\S]*){2}"),
                    timeout=TIMEOUT)
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
        vm.wait_for(re.compile(r"(@RC stage=uos ev=wdog result=on[^\n]*\n[\s\S]*){2}"),
                    timeout=TIMEOUT)
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


PINFAULT = "err %DEVICE-E-PINFAULT, a pin write failed and is retried; command refused"


def test_pulse_end_fault(board, hooks_image, need_mtools, tmp_path):
    """The pulse end's write fails 300 times: the status says UNLOCKED with
    the fault, commands are refused, each tick retries, success clears it."""
    with alone(hooks_image, board, boot_card(tmp_path)) as vm:
        wait_ready(vm)
        assert dev_set(vm, "BOLT", "UNLOCK").status == "ok"
        assert answer(vm, "dev fail BOLT 300") == ["dev name=BOLT fail=300"]
        wait_mark(vm, "devfault name=BOLT result=failing")
        f = one(vm, "devfault")
        assert (f["op"], f["owed"], f["why"]) == ("pulse", "0", "IOERR"), f
        row = dev_list(vm)["BOLT"]
        assert " UNLOCKED " in row and " FAULT=" in row, row
        assert dev_set(vm, "BOLT", "LOCK").status == PINFAULT
        wait_mark(vm, "devfault name=BOLT result=cleared")
        done = marks(vm, "devfault", result="cleared")
        assert len(done) == 1 and (done[0]["fails"], done[0]["state"]) == ("300", "LOCKED"), done
        # One marker at each end, none per retry.
        assert len(events(vm, "devfault")) == 2, vm.serial
        row = dev_list(vm)["BOLT"]
        assert " LOCKED " in row and "FAULT" not in row, row
        assert pins(record(vm))[-1] == (23, 0)
        assert dev_set(vm, "BOLT", "LOCK").status == "ok"
        assert vm.script("dev fail PIR x", timeout=TIMEOUT).status.startswith("err usage")


def test_safe_state_arm_fails(board, hooks_image, need_mtools, tmp_path):
    """ARM's off write fails at reboot: 3 tries, then every other output
    still goes to its initial level."""
    with alone(hooks_image, board, boot_card(tmp_path)) as vm:
        wait_ready(vm)
        assert dev_set(vm, "DOOR1", "OPEN").status == "ok"
        assert answer(vm, "dev fail ARM 1000") == ["dev name=ARM fail=1000"]
        vm.send("reboot\n", port=1)
        vm.wait_for(READY_TWICE, timeout=TIMEOUT)
        vm.wait_for(re.compile(r"(@RC stage=uos ev=wdog result=on[^\n]*\n[\s\S]*){2}"),
                    timeout=TIMEOUT)
        safe = events(vm, "devsafe")
        assert [(int(m["pin"]), int(m["level"])) for m in safe] == ALL_SAFE, vm.serial
        assert (safe[0]["name"], safe[0]["tries"]) == ("ARM", "3") and safe[0]["rc"] != "0"
        assert all((m["rc"], m["tries"]) == ("0", "1") for m in safe[1:]), safe
        assert events(vm, "devdown")[0]["count"] == str(len(ALL_SAFE))
        # The new boot clears the hook: a normal boot record.
        assert pins(record(vm)) == ALL_BOOT
