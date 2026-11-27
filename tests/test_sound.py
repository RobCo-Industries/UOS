"""The sound path: [audio] in UOS.CFG and the sound service.

With the default `sound = off` UOS never starts the audio driver, so no
audio trace line shows. With `sound = on` the first audio line is the
library's rest ramp: 882 samples at 22050 Hz, then its end. QEMU has no
PWM, so the library traces every play on the serial line.
"""

import re

import pytest

from test_buttons import PINS
from test_dev import CFG as DEV_CFG
from test_dev import wait_ready as wait_dev
from test_shell import login, shell_card
from uos import IDLE, TIMEOUT, alone, answer, events, one, uos_card, wait_marker, wait_ready

ON_CFG = b"[system]\nname = TEST TERMINAL\n[audio]\nsound = on\nvolume = 40\n"
AUDIO = re.compile(r"@RC stage=uos ev=audio[ _]")
RAMP_END = re.compile(r"@RC stage=uos ev=audio op=pcm_end [^\n]*\n")


def audio_lines(vm):
    return [m for m in vm.markers() if m.get("ev") == "audio"]


def check_ramp(vm):
    vm.wait_for(RAMP_END, timeout=TIMEOUT)
    lines = audio_lines(vm)
    assert lines[0]["op"] == "pcm", vm.serial
    assert (lines[0]["rate"], lines[0]["len"], lines[0]["vol"]) == ("22050", "882", "100"), lines[0]
    # n counts the samples written; a late IRQ in QEMU skips some. last is
    # the index of the final one: the ramp reached its end.
    assert lines[1]["op"] == "pcm_end" and lines[1]["last"] == "881", lines[1]
    assert 0 < int(lines[1]["n"]) <= 882, lines[1]
    return lines


def test_off_gives_no_audio_line(board, hooks_image, need_mtools, tmp_path):
    """The default: no audio_init(), no audio line through a boot and a login."""
    with alone(hooks_image, board, shell_card(tmp_path)) as vm:
        wait_ready(vm)
        s = one(vm, "sound")
        assert (s["result"], s["rc"]) == ("off", "0"), s
        c = one(vm, "config")
        assert (c["sound"], c["fan"], c["volume"]) == ("0", "0", "80"), c
        login(vm, "ADMIN")
        answer(vm, "state")
        assert not AUDIO.search(vm.serial), vm.serial


def test_on_plays_the_ramp_first(board, uos_image, need_mtools, tmp_path):
    """sound = on: the service starts, and the ramp is the first audio line."""
    with alone(uos_image, board, uos_card(tmp_path, cfg=ON_CFG)) as vm:
        wait_ready(vm)
        s = one(vm, "sound")
        assert (s["result"], s["rc"]) == ("on", "0"), s
        c = one(vm, "config")
        assert (c["sound"], c["fan"], c["volume"]) == ("1", "0", "40"), c
        lines = check_ramp(vm)
        assert len(lines) == 2, lines


def test_on_with_watchdog_and_buttons(board, uos_image, need_mtools, tmp_path):
    """The device watchdog armed and the buttons active: audio takes the last tick slot."""
    devs = b"[device TUR1]\ntype = turret\ngpio = 16\n"
    sd = uos_card(tmp_path, cfg=ON_CFG, extra={DEV_CFG: devs})
    with alone(uos_image, board, sd, qtest=True, gpio_high=PINS.values()) as vm:
        wait_dev(vm)
        assert one(vm, "buttons")["result"] == "active", vm.serial
        assert one(vm, "wdog")["result"] == "on", vm.serial
        s = one(vm, "sound")
        assert (s["result"], s["rc"]) == ("on", "0"), s
        check_ramp(vm)


@pytest.mark.parametrize("how", ["reboot", "halt"])
def test_stop_after_the_ramp(board, hooks_image, need_mtools, tmp_path, how):
    """A reboot or a halt after the ramp's end: the service stops audio, which
    prints nothing more, as nothing plays and no poll runs after it."""
    with alone(hooks_image, board, uos_card(tmp_path, cfg=ON_CFG)) as vm:
        wait_ready(vm)
        check_ramp(vm)
        if how == "reboot":
            vm.send("reboot\n", port=1)
            wait_marker(vm, "ready", 2)
        else:
            answer(vm, how)
            vm.wait_for(IDLE, timeout=TIMEOUT)
        assert [m["name"] for m in events(vm, "shutdown")][-1] == "sound", vm.serial
        first = vm.serial.index("@RC stage=uos ev=shutdown name=sound")
        end = vm.serial.index("@RC stage=uos ev=boot ", first) if how == "reboot" else None
        assert not AUDIO.search(vm.serial[first:end]), vm.serial[first:]
