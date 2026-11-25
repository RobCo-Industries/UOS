"""Entry: every bring-up marker in order, the trust rule, the screen and the self check."""

from harness.boot import BOARDS

from uos import (BOOT_ORDER, TIMEOUT, after_chain, after_chain1, alone, events, one, order,
                 state, uos_card, wait_ready)


def test_markers_in_order_alone(board, uos_image, need_mtools, tmp_path):
    with alone(uos_image, board, uos_card(tmp_path)) as vm:
        wait_ready(vm)
        assert [e for e in order(vm) if e in BOOT_ORDER] == BOOT_ORDER, vm.serial
        boot = one(vm, "boot")
        assert boot["version"] == "0.1.0"
        # Alone, the stage starts in the mode the firmware stub uses on this board.
        assert boot["entry_mode"] == BOARDS[board].auto_mode
        handoff = one(vm, "handoff")
        assert (handoff["trusted"], handoff["prev"]) == ("0", "0"), handoff
        assert one(vm, "maint")["mini"] == "0"
        assert one(vm, "mbox")["rc"] == "0"
        mmu = one(vm, "mmu")
        assert (mmu["result"], mmu["rc"]) == ("on", "0"), mmu
        screen = one(vm, "screen")
        assert (screen["fb"], screen["console"], screen["scheme"], screen["source"]) == \
            ("0", "0", "0", "default"), screen
        tick = one(vm, "tick")
        assert (tick["hz"], tick["rc"]) == ("100", "0"), tick
        heap = one(vm, "heap")
        assert heap["base"] == "0x01000000" and int(heap["size"]) > 0, heap
        inp = one(vm, "input")
        assert (inp["serial"], inp["script"], inp["gpio"]) == ("0", "0", "off"), inp
        assert one(vm, "selfcheck")["rc"] == "ok"
        assert one(vm, "mount")["result"] == "ok"
        assert one(vm, "config")["result"] == "ok"
        # QEMU has no clock chip on I2C.
        assert one(vm, "rtc")["present"] == "0"
        ready = one(vm, "ready")
        assert (ready["screen"], ready["card"]) == ("first", "ok"), ready
        st = state(vm)
        assert st == {"phase": "ready", "screen": "first", "user": "none", "card": "ok",
                      "trusted": "0", "error": "none"}, st
        assert not events(vm, "panic")


def test_trusted_after_chain1(board, chain1_image, uos_image, need_mtools, tmp_path):
    """chain1 builds a boot info and an amber console: UOS trusts the block and keeps
    the colour scheme, but clears the screen."""
    with after_chain1(chain1_image, uos_image, board, uos_card(tmp_path)) as vm:
        wait_ready(vm)
        assert [e for e in order(vm) if e in BOOT_ORDER] == BOOT_ORDER, vm.serial
        handoff = one(vm, "handoff")
        assert (handoff["trusted"], handoff["prev"]) == ("1", "1"), handoff
        screen = one(vm, "screen")
        assert (screen["scheme"], screen["source"]) == ("1", "bootinfo"), screen
        assert state(vm)["trusted"] == "1"


def test_bad_crc_is_not_trusted(board, chain1_bad_image, uos_image, need_mtools, tmp_path):
    """chain1 breaks the boot info CRC after sealing it: UOS must cold-boot."""
    with after_chain1(chain1_bad_image, uos_image, board, uos_card(tmp_path)) as vm:
        wait_ready(vm)
        assert one(vm, "jump", "chain1")
        handoff = one(vm, "handoff")
        assert (handoff["trusted"], handoff["prev"]) == ("0", "0"), handoff
        assert one(vm, "screen")["source"] == "default"


RETROS_BOOT_CFG = b"""[boot]
default = UOS
timeout = 0

[entry UOS]
path = /ROBCO/UOS.SYS

[bios]
delay_ms = 0
beep = off
"""


def test_whole_chain(board, chain_images, uos_bytes, need_mtools, tmp_path):
    retros, mfboot = chain_images
    with open(mfboot, "rb") as f:
        agent = f.read()
    sd = uos_card(tmp_path, extra={"/ROBCO/MFBOOT.SYS": agent, "/ROBCO/UOS.SYS": uos_bytes,
                                   "/ROBCO/BOOT.CFG": RETROS_BOOT_CFG})
    with after_chain(retros, board, sd) as vm:
        wait_ready(vm)
        assert one(vm, "jump", "bootagent")
        handoff = one(vm, "handoff")
        assert (handoff["trusted"], handoff["prev"]) == ("1", "2"), handoff
        assert [e for e in order(vm) if e in BOOT_ORDER] == BOOT_ORDER, vm.serial
