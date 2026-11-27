"""The fan, with `[audio] sound = on`, `fan = on` and `trace = on`.

Once the rest ramp has ended, the fan plays its whole table, and a second
pass waits behind it, so the trace shows `op=pcm_next` with the fan's CRC
about every 2.2 s. A one-shot over the fan plays its head, the one-shot
mixed with the fan's first bytes, as `op=pcm`, then the fan's rest as
`op=pcm_next`. Each line is read by the CRCs of the generated header.
"""

import os
import re

from test_login import login
from test_sound import PICKS, check_ramp, login_card, user_prompt
from uos import ROOT, TIMEOUT, alone, wait_marker, wait_ready

FAN_CFG = (b"[system]\nname = TEST TERMINAL\n[audio]\nsound = on\nfan = on\n"
           b"volume = 40\ntrace = on\n")
NOFAN_CFG = (b"[system]\nname = TEST TERMINAL\n[audio]\nsound = on\nfan = off\n"
             b"volume = 40\ntrace = on\n")
FAN_MS = 83374 * 1000 / 37500


def header():
    """{NAME_FIELD: value} for every CRC32 and LEN macro of the generated header."""
    with open(os.path.join(ROOT, "src", "sfx", "sfx_table.h")) as f:
        h = f.read()
    out = {m.group(1): m.group(2)
           for m in re.finditer(r"#define UOS_SFX_(\w+_CRC32) 0x([0-9a-f]{8})u", h)}
    out.update({m.group(1): m.group(2)
                for m in re.finditer(r"#define UOS_SFX_(\w+_LEN) (\d+)u", h)})
    return out


H = header()


def line_re(op, length, crc):
    return re.compile(rf"@RC stage=uos ev=audio op={op} (?:rate=37500 )?len={length} "
                      rf"(?:vol=\d+ )?crc={crc} t_ms=(\d+)[^\n]*\n")


def fan_pcm():
    return line_re("pcm", H["FAN_LEN"], H["FAN_CRC32"])


def fan_next():
    return line_re("pcm_next", H["FAN_LEN"], H["FAN_CRC32"])


def audio(vm):
    return [m for m in vm.markers() if m.get("ev") == "audio"]


def check_head_rest(vm, sound, start):
    """From serial offset `start`: `sound`'s head as a play, then its rest
    as the next queued buffer."""
    up = sound.upper()
    head = line_re("pcm", H[up + "_HEAD_LEN"], H[up + "_HEAD_CRC32"])
    rest = line_re("pcm_next", H[up + "_REST_LEN"], H[up + "_REST_CRC32"])
    vm.wait_for(re.compile(head.pattern + r"[\s\S]*?" + rest.pattern), timeout=TIMEOUT)
    text = vm.serial[start:]
    h = head.search(text)
    assert h, text
    n = re.search(r"@RC stage=uos ev=audio op=pcm_next [^\n]*\n", text[h.end():])
    assert n and rest.match(n.group(0)), text[h.start():]


def test_fan_starts_after_the_ramp_and_keeps_running(board, uos_image, need_mtools, tmp_path):
    """The first play after the ramp is the fan's table, and a queued pass
    joins it about every 2.2 s."""
    with alone(uos_image, board, login_card(tmp_path, FAN_CFG)) as vm:
        wait_ready(vm)
        check_ramp(vm)
        vm.wait_for(fan_pcm(), timeout=TIMEOUT)
        third = re.compile(r"(?:" + fan_next().pattern + r"[\s\S]*?){2}")
        vm.wait_for(third, timeout=TIMEOUT + 3 * FAN_MS / 1000)
        lines = audio(vm)
        assert lines[2]["op"] == "pcm" and lines[2]["len"] == H["FAN_LEN"], lines
        assert lines[2]["crc"] == H["FAN_CRC32"], lines
        t = [int(lines[2]["t_ms"])] + [int(m["t_ms"]) for m in lines[3:]
                                       if m["op"] == "pcm_next"]
        assert len(t) >= 3, lines
        for a, b in zip(t, t[1:]):
            assert abs((b - a) - FAN_MS) <= 60, t


def test_key_plays_its_head_then_the_rest(board, uos_image, need_mtools, tmp_path):
    """A key over the fan: its head, then the fan's rest behind it."""
    with alone(uos_image, board, login_card(tmp_path, FAN_CFG)) as vm:
        wait_ready(vm)
        check_ramp(vm)
        vm.wait_for(fan_pcm(), timeout=TIMEOUT)
        user_prompt(vm)
        start = len(vm.serial)
        vm.send("A", port=0)
        check_head_rest(vm, "key_" + PICKS[0], start)


def test_bad_password_plays_its_head_then_the_rest(board, uos_image, need_mtools, tmp_path):
    """A bad password over the fan: pass_bad's head, then the fan's rest."""
    with alone(uos_image, board, login_card(tmp_path, FAN_CFG)) as vm:
        wait_ready(vm)
        check_ramp(vm)
        vm.wait_for(fan_pcm(), timeout=TIMEOUT)
        start = len(vm.serial)
        login(vm, "ADMIN", "WRONG-PASSWORD!", expect="bad")
        check_head_rest(vm, "pass_bad", start)


def test_fan_off_plays_the_plain_table(board, uos_image, need_mtools, tmp_path):
    """fan = off: no fan, and a key plays its plain table with nothing queued."""
    with alone(uos_image, board, login_card(tmp_path, NOFAN_CFG)) as vm:
        wait_ready(vm)
        check_ramp(vm)
        user_prompt(vm)
        vm.send("A", port=0)
        key = "KEY_" + PICKS[0].upper()
        vm.wait_for(line_re("pcm", H[key + "_LEN"], H[key + "_CRC32"]), timeout=TIMEOUT)
        assert "op=pcm_next" not in vm.serial, vm.serial
        assert H["FAN_CRC32"] not in vm.serial, vm.serial


def test_reboot_stops_the_fan(board, hooks_image, need_mtools, tmp_path):
    """A reboot while the fan runs: the service stops it, and no top-up
    starts it again before the next boot."""
    with alone(hooks_image, board, login_card(tmp_path, FAN_CFG)) as vm:
        wait_ready(vm)
        check_ramp(vm)
        vm.wait_for(fan_pcm(), timeout=TIMEOUT)
        vm.send("reboot\n", port=1)
        wait_marker(vm, "ready", 2)
        first = vm.serial.index("@RC stage=uos ev=shutdown name=sound")
        end = vm.serial.index("@RC stage=uos ev=boot ", first)
        assert "ev=audio" not in vm.serial[first:end], vm.serial[first:]
