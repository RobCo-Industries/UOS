"""The first screen: a golden of its fixed text, and a screenshot to look at."""

import os
import re

from uos import IDLE, TIMEOUT, alone, answer, check_golden, dump, screenshot, uos_card, \
    wait_ready


def test_first_screen_golden(board, uos_image, need_mtools, tmp_path):
    """Fixed text only: the card is known, QEMU has no clock chip, no key was pressed."""
    with alone(uos_image, board, uos_card(tmp_path)) as vm:
        wait_ready(vm)
        head, rows = dump(vm)
        assert (head["cols"], head["rows"]) == ("80", "30"), head
        check_golden(rows, "first")


def test_first_screen_after_a_key(board, uos_image, need_mtools, tmp_path):
    with alone(uos_image, board, uos_card(tmp_path)) as vm:
        wait_ready(vm)
        answer(vm, "key left")
        vm.wait_for(re.compile(r"@RC stage=uos ev=key [^\n]*\n"), timeout=TIMEOUT)
        _, rows = dump(vm)
        assert "KEYS: 1, LAST left" in [r.rstrip() for r in rows], rows


def test_screenshots(board, uos_image, hooks_image, need_mtools, tmp_path):
    """Screenshots of the first screen and the panic screen, saved for a person to look
    at. ROBCO_SHOT_DIR names a folder to keep them in."""
    out = os.environ.get("ROBCO_SHOT_DIR")
    with alone(uos_image, board, uos_card(tmp_path)) as vm:
        wait_ready(vm)
        first = screenshot(vm, tmp_path, "first")
    with alone(hooks_image, board, uos_card(tmp_path, name="sd2.img")) as vm:
        wait_ready(vm)
        answer(vm, "fault undef")
        vm.wait_for(IDLE, timeout=TIMEOUT)
        panic = screenshot(vm, tmp_path, "panic")
    assert first.size == (640, 480) and panic.size == (640, 480)
    assert first.getbbox() is not None and panic.getbbox() is not None
    if out:
        first.save(os.path.join(out, f"uos-first-{board}.png"))
        panic.save(os.path.join(out, f"uos-panic-{board}.png"))
