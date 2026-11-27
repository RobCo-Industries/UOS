"""The sound path: [audio] in UOS.CFG and the sound service.

With the default `sound = off` UOS never starts the audio driver, so no
audio trace line shows. With `sound = on` the first audio line is the
library's rest ramp: 882 samples at 22050 Hz, then its end. QEMU has no
PWM, so the library traces every play, and with `trace = on` UOS prints
each record on the serial line. With `trace = off` it drops them.

A key the line editor takes plays key_a or key_b at 37500 Hz, picked by
the generator of src/sfx/sfxrules.c from seed 1.
"""

import os
import re

import pytest

from test_buttons import PINS
from test_dev import CFG as DEV_CFG
from test_dev import wait_ready as wait_dev
from test_shell import login, shell_card
from uos import (IDLE, ROOT, TIMEOUT, alone, answer, card, card_tree, events, one, uos_card,
                 wait_marker, wait_ready)

ON_CFG = b"[system]\nname = TEST TERMINAL\n[audio]\nsound = on\nvolume = 40\ntrace = on\n"
DEFAULT_OFF = b"[system]\nname = TEST TERMINAL\n"
QUIET_CFG = b"[system]\nname = TEST TERMINAL\n[audio]\nsound = on\nvolume = 40\n"
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
        assert (c["sound"], c["fan"], c["volume"], c["trace"]) == ("0", "0", "80", "0"), c
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
        assert (c["sound"], c["fan"], c["volume"], c["trace"]) == ("1", "0", "40", "1"), c
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


# The first keys from seed 1: a for key_a, b for key_b. tests/host/test_sfx.c
# pins the same 32.
PICKS = "aabbbbabaabbaabbbbbaaabbbabbaaab"
KEY_PCM = re.compile(r"@RC stage=uos ev=audio op=pcm rate=37500 [^\n]*\n")
EL = "\x1b[K"


def key_crcs():
    """The CRC-32 of key_a and key_b, from the generated header."""
    with open(os.path.join(ROOT, "src", "sfx", "sfx_table.h")) as f:
        h = f.read()
    return {k: re.search(r"#define UOS_SFX_KEY_%s_CRC32 0x([0-9a-f]{8})u" % k.upper(), h).group(1)
            for k in "ab"}


def key_lines(vm):
    return [m for m in audio_lines(vm) if m["op"] == "pcm" and m["rate"] == "37500"]


def login_card(tmp_path, cfg):
    """The default card with its users, so the login prompt shows."""
    return card(tmp_path, card_tree({"/UOS/UOS.CFG": cfg}))


def user_prompt(vm):
    wait_marker(vm, "login state=user")


def type_keys(vm, text, plays):
    """Types `text` and Enter on the PL011, one key at a time. With `plays`,
    waits for each key's play line: two keys in one tick would merge."""
    for ch in text + "\r":
        n = len(key_lines(vm))
        vm.send(ch, port=0)
        if plays:
            vm.wait_for(re.compile(r"(?:" + KEY_PCM.pattern + r"[\s\S]*?){" + str(n + 1) + "}"),
                        timeout=TIMEOUT)
        else:
            vm.wait_for(re.compile(r"\r[^\n@]*" + re.escape(ch) + re.escape(EL))
                        if ch != "\r" else re.compile(r"login state=pass"), timeout=TIMEOUT)


def test_keys_click_in_order(board, uos_image, need_mtools, tmp_path):
    """Five characters and Enter: six plays at 37500 Hz, key_a or key_b in
    the pinned order, each the table's own CRC."""
    crcs = key_crcs()
    with alone(uos_image, board, login_card(tmp_path, ON_CFG)) as vm:
        wait_ready(vm)
        check_ramp(vm)
        user_prompt(vm)
        type_keys(vm, "ADMIN", plays=True)
        wait_marker(vm, "login state=pass")
        lines = key_lines(vm)
        assert len(lines) == 6, lines
        assert [m["crc"] for m in lines] == [crcs[k] for k in PICKS[:6]], lines
        assert all(m["vol"] == "40" for m in lines), lines


def test_markers_never_split_the_echo(board, uos_image, need_mtools, tmp_path):
    """With the trace on, each audio line starts a line of its own, and the
    next echo draws the whole input line again."""
    with alone(uos_image, board, login_card(tmp_path, ON_CFG)) as vm:
        wait_ready(vm)
        check_ramp(vm)
        user_prompt(vm)
        start = len(vm.serial)
        type_keys(vm, "ADMIN", plays=True)
        wait_marker(vm, "login state=pass")
        text = vm.serial[start:]
        assert all(i == 0 or text[i - 1] == "\n" for i in
                   (m.start() for m in re.finditer("@RC ", text))), repr(text)
        echoes = [l for l in text.split("\n") if l and not l.startswith("@RC ")]
        assert echoes and all(l.startswith("\r") for l in echoes), repr(text)
        assert any("USERNAME: ADMIN" + EL in l for l in echoes), repr(text)


def test_trace_off_prints_no_audio_line(board, uos_image, need_mtools, tmp_path):
    """Sound on, trace off: the keys play, but no audio line shows, and the
    echoed input line stays whole on one line."""
    with alone(uos_image, board, login_card(tmp_path, QUIET_CFG)) as vm:
        wait_ready(vm)
        s = one(vm, "sound")
        assert (s["result"], s["rc"]) == ("on", "0"), s
        assert one(vm, "config")["trace"] == "0", vm.serial
        user_prompt(vm)
        start = len(vm.serial)
        type_keys(vm, "ADMIN", plays=False)
        wait_marker(vm, "login state=pass")
        assert not AUDIO.search(vm.serial), vm.serial
        line = vm.serial[start:].split("\n")[0].rstrip("\r")
        assert line.startswith("\rUSERNAME: " + EL), repr(line)
        assert line.endswith("\rUSERNAME: ADMIN" + EL), repr(line)


def test_off_typing_gives_no_audio_line(board, uos_image, need_mtools, tmp_path):
    """Sound off: typing plays nothing and prints no audio line."""
    with alone(uos_image, board, login_card(tmp_path, DEFAULT_OFF)) as vm:
        wait_ready(vm)
        assert one(vm, "sound")["result"] == "off", vm.serial
        user_prompt(vm)
        type_keys(vm, "ADMIN", plays=False)
        wait_marker(vm, "login state=pass")
        assert not AUDIO.search(vm.serial), vm.serial



def test_key_at_service_start_keeps_the_ramp(board, uos_image, need_mtools, tmp_path):
    """A key sent at the sound marker, while the ramp may still play: no key
    sound, and the ramp reaches its last sample. A key at the prompt clicks.
    In QEMU the first screen takes that early key, and the line editor runs
    only after the ramp's end. tests/host/test_sfx.c covers the rest gate
    that drops an effect before the ramp ends."""
    with alone(uos_image, board, login_card(tmp_path, ON_CFG)) as vm:
        vm.wait_for(re.compile(r"@RC stage=uos ev=sound result=on [^\n]*\n"), timeout=TIMEOUT)
        vm.send("A", port=0)
        lines = check_ramp(vm)
        user_prompt(vm)
        assert not key_lines(vm), lines
        type_keys(vm, "A", plays=True)
        assert len(key_lines(vm)) == 2, audio_lines(vm)
