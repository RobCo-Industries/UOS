"""The six panel buttons, driven through qtest with harness.pigpio.

QEMU models no pulls, so an undriven line reads 0, which an active-low
button reads as pressed. A plain boot then reads every line pressed for
the whole guard and disables the buttons. A test that wants working
buttons drives every line high before the guest starts.

No check here depends on host timing. A press is held until the guest
reports its key, then released, so a host stall can only make the test
slower.
"""

import time

import pytest

from harness.pigpio import PiGpio
from test_hack import cfg as hack_cfg
from test_hack import hack_card
from test_hack import state as hack_state
from test_menu import menu_card, menu_state, wait_menu
from test_dev import CFG as DEV_CFG
from test_dev import dev_list
from test_dev import wait_ready as wait_dev
from test_shell import expect, login, n_marks, out, shell_card
from uos import (DEFAULT_CFG, TIMEOUT, alone, answer, dump, events, one, uos_card,
                 wait_marker, wait_ready)

# src/kernel/buttons.c, BCM numbers.
PINS = {"up": 5, "down": 6, "left": 17, "right": 22, "select": 26, "back": 27}
OFF_CFG = b"[system]\nname = TEST TERMINAL\n[input]\nbuttons = off\n"


def keys(vm):
    return events(vm, "key")


def wait_keys(vm, n):
    deadline = time.monotonic() + TIMEOUT
    while len(keys(vm)) < n:
        assert time.monotonic() < deadline, vm.serial
        time.sleep(0.02)
    return keys(vm)


def hold_until_key(io, vm, button):
    """Presses `button` until one more key marker shows, then releases it."""
    n = len(keys(vm))
    io.press(button)
    try:
        return wait_keys(vm, n + 1)[n]
    finally:
        io.release(button)


def test_plain_boot_disables_stuck_lines(board, uos_image, need_mtools, tmp_path):
    """No line driven: all six read pressed, the guard disables them, no key comes."""
    with alone(uos_image, board, uos_card(tmp_path)) as vm:
        wait_ready(vm)
        b = one(vm, "buttons")
        assert (b["result"], b["rc"], b["setting"], b["repeat"]) == \
            ("disabled_stuck", "0", "1", "none"), b
        assert one(vm, "config")["buttons"] == "1"
        assert one(vm, "input")["gpio"] == "later"
        # A command answered after ready: the loop ran, and still no key.
        answer(vm, "state")
        assert keys(vm) == [], vm.serial


def test_off_touches_no_pin(board, uos_image, need_mtools, tmp_path):
    """buttons = off: no start, and a press gives no key."""
    with alone(uos_image, board, uos_card(tmp_path, cfg=OFF_CFG), qtest=True,
               gpio_high=PINS.values()) as vm:
        io = PiGpio(vm, board, names=PINS)
        wait_ready(vm)
        b = one(vm, "buttons")
        assert (b["result"], b["rc"], b["setting"], b["repeat"], b["ticks"]) == \
            ("off", "0", "0", "none", "0"), b
        assert one(vm, "config")["buttons"] == "0"
        io.press("select")
        time.sleep(0.2)
        io.release("select")
        # A key through the port after the press: only that one shows.
        answer(vm, "key up")
        got = wait_keys(vm, 1)
        assert [k["name"] for k in got] == ["up"], vm.serial


def test_every_button_reaches_the_first_screen(board, uos_image, need_mtools, tmp_path):
    """Lines high: the guard passes, repeat runs, and each button gives its key."""
    with alone(uos_image, board, uos_card(tmp_path), qtest=True,
               gpio_high=PINS.values()) as vm:
        io = PiGpio(vm, board, names=PINS)
        wait_ready(vm)
        b = one(vm, "buttons")
        assert (b["result"], b["rc"], b["setting"], b["repeat"]) == \
            ("active", "0", "1", "0"), b
        assert 10 <= int(b["ticks"]) <= 30, b
        for name in PINS:
            k = hold_until_key(io, vm, name)
            assert (k["screen"], k["type"], k["name"]) == ("first", "key", name), k


def test_show_system_line(board, hooks_image, need_mtools, tmp_path):
    with alone(hooks_image, board, shell_card(tmp_path)) as vm:
        wait_ready(vm)
        login(vm, "ADMIN")
        _, _, s = expect(vm, "SHOW SYSTEM", "NORMAL")
        assert "Buttons disabled_stuck" in out(vm, s), out(vm, s)


@pytest.mark.parametrize("cfg", [DEFAULT_CFG, OFF_CFG], ids=["on", "off"])
def test_button_pins_refused_even_off(board, uos_image, need_mtools, tmp_path, cfg):
    """Pin 16 is free for a device. Pin 5 is a button pin: PINUSED, buttons on or off."""
    devs = b"[device TUR1]\ntype = turret\ngpio = 16\n[device UPX]\ntype = light\ngpio = 5\n"
    sd = uos_card(tmp_path, cfg=cfg, extra={DEV_CFG: devs})
    with alone(uos_image, board, sd) as vm:
        wait_dev(vm)
        assert one(vm, "buttons")["setting"] == ("1" if cfg == DEFAULT_CFG else "0")
        errs = [(m["name"], m["why"]) for m in events(vm, "deverr")]
        assert errs == [("UPX", "PINUSED")], errs
        assert list(dev_list(vm)) == ["TUR1"]


def hold_until(io, vm, button, done):
    """Presses `button` until `done()` is true, then releases it."""
    deadline = time.monotonic() + TIMEOUT
    io.press(button)
    try:
        while not done():
            assert time.monotonic() < deadline, vm.serial
            time.sleep(0.02)
    finally:
        io.release(button)


def osk_fields(vm):
    return dict(f.split("=", 1) for f in answer(vm, "osk state")[0].split()[1:])


def at_user(vm):
    wait_ready(vm)
    wait_marker(vm, "login state=user", 1)


def user_row(vm):
    rows = dump(vm)[1]
    hit = [r.rstrip() for r in rows if r.startswith("USERNAME:")]
    return hit[0] if hit else None


def test_button_at_login_types_with_the_keyboard(board, hooks_image, need_mtools, tmp_path):
    """A button at USERNAME: opens the keyboard; RIGHT then SELECT types B."""
    with alone(hooks_image, board, menu_card(tmp_path), qtest=True,
               gpio_high=PINS.values()) as vm:
        io = PiGpio(vm, board, names=PINS)
        at_user(vm)
        hold_until(io, vm, "down", lambda: "osk state=open" in vm.serial)
        assert (osk_fields(vm)["row"], osk_fields(vm)["col"]) == ("0", "0")
        hold_until(io, vm, "right", lambda: osk_fields(vm)["col"] == "1")
        hold_until(io, vm, "select", lambda: user_row(vm) == "USERNAME: B")
        assert "hack op=start" not in vm.serial


def test_buttons_run_a_menu_entry(board, hooks_image, need_mtools, tmp_path):
    """After the login, DOWN and SELECT by buttons move and run a menu entry."""
    with alone(hooks_image, board, menu_card(tmp_path), qtest=True,
               gpio_high=PINS.values()) as vm:
        io = PiGpio(vm, board, names=PINS)
        at_user(vm)
        n = n_marks(vm, "session screen=menu")
        answer(vm, "sess login OPER")
        wait_marker(vm, "session screen=menu", n + 1)
        wait_menu(vm, state="menu", level="1", sel="0")
        hold_until(io, vm, "down", lambda: menu_state(vm)["sel"] == "1")
        hold_until(io, vm, "select", lambda: menu_state(vm)["level"] == "2")
        wait_menu(vm, level="2", file="SUB.CFG")


def test_back_at_empty_user_starts_the_game(board, hooks_image, need_mtools, tmp_path):
    """BACK at an empty USERNAME: starts the game; DOWN moves its cursor."""
    with alone(hooks_image, board, hack_card(tmp_path, hack_cfg()), qtest=True,
               gpio_high=PINS.values()) as vm:
        io = PiGpio(vm, board, names=PINS)
        at_user(vm)
        hold_until(io, vm, "back", lambda: "hack op=start" in vm.serial)
        wait_marker(vm, "login state=hack", 1)
        assert "osk state=open" not in vm.serial
        before = hack_state(vm).f["cursor"]
        hold_until(io, vm, "down", lambda: hack_state(vm).f["cursor"] != before)
