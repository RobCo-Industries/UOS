"""The card and UOS.CFG: the degraded modes and the settings that apply."""

from uos import (BOOT_ORDER, alone, card, dump, events, one, order, state, uos_card,
                 wait_ready)


def screen_has(rows, text):
    return any(r.rstrip() == text for r in rows)


def test_no_card(board, uos_image):
    with alone(uos_image, board) as vm:
        wait_ready(vm)
        assert order(vm) == BOOT_ORDER, vm.serial
        mount = one(vm, "mount")
        assert (mount["result"], mount["why"]) == ("none", "sd"), mount
        assert one(vm, "config")["result"] == "skipped"
        assert one(vm, "ready")["card"] == "none"
        st = state(vm)
        assert (st["phase"], st["card"], st["error"]) == ("ready", "none", "nocard"), st
        _, rows = dump(vm)
        assert screen_has(rows, "CARD: NO CARD. INSERT A CARD AND RESTART."), rows
        assert screen_has(rows, "ROBCO TERMINAL"), rows


def test_no_uos_tree(board, uos_image, need_mtools, tmp_path):
    sd = card(tmp_path, {"/ROBCO/README.TXT": b"not a UOS card\n"})
    with alone(uos_image, board, sd) as vm:
        wait_ready(vm)
        assert one(vm, "mount")["result"] == "notree"
        assert one(vm, "config")["result"] == "skipped"
        st = state(vm)
        assert (st["card"], st["error"]) == ("notree", "notree"), st
        _, rows = dump(vm)
        assert screen_has(rows, "CARD: NO /UOS FOLDER. SYSTEM FILES ARE MISSING."), rows


def test_missing_cfg(board, uos_image, need_mtools, tmp_path):
    with alone(uos_image, board, uos_card(tmp_path, cfg=None)) as vm:
        wait_ready(vm)
        assert one(vm, "mount")["result"] == "ok"
        cfg = one(vm, "config")
        assert (cfg["result"], cfg["name"]) == ("missing", "ROBCO_TERMINAL"), cfg
        assert state(vm)["error"] == "none"


def test_cfg_syntax_error(board, uos_image, need_mtools, tmp_path):
    text = b"[system]\nname = EARLY\nthis line is not ini\nname = LATE\n"
    with alone(uos_image, board, uos_card(tmp_path, cfg=text)) as vm:
        wait_ready(vm)
        cfg = one(vm, "config")
        assert (cfg["result"], cfg["line"], cfg["name"]) == ("syntax", "3", "EARLY"), cfg
        assert state(vm)["error"] == "config"
        _, rows = dump(vm)
        assert screen_has(rows, "SETTINGS: SOME VALUES IGNORED"), rows
        assert screen_has(rows, "EARLY"), rows


def test_cfg_bad_and_clamped_values(board, uos_image, need_mtools, tmp_path):
    text = (b"[system]\nscheme = purple\nname = ROOM 4\n"
            b"[login]\nretries = 0\nlockout_s = 9000\n"
            b"[hack]\nattempts = 12\ndifficulty = easy\nsound = on\n")
    with alone(uos_image, board, uos_card(tmp_path, cfg=text)) as vm:
        wait_ready(vm)
        cfg = one(vm, "config")
        assert cfg["result"] == "ok"
        assert (cfg["bad"], cfg["clamped"], cfg["unknown"]) == ("2", "3", "1"), cfg
        assert (cfg["retries"], cfg["login_lockout"], cfg["attempts"]) == ("1", "3600", "9"), cfg
        assert (cfg["scheme"], cfg["difficulty"]) == ("default", "advanced"), cfg
        assert cfg["name"] == "ROOM_4"
        assert cfg["console"] == "0"


def test_cfg_applies_scheme_and_name(board, uos_image, need_mtools, tmp_path):
    text = b"[system]\nname = Overseer Office\nscheme = amber\nmode = terminal\n"
    with alone(uos_image, board, uos_card(tmp_path, cfg=text)) as vm:
        wait_ready(vm)
        cfg = one(vm, "config")
        assert (cfg["result"], cfg["scheme"], cfg["mode"], cfg["console"]) == \
            ("ok", "amber", "terminal", "1"), cfg
        assert (cfg["bad"], cfg["clamped"], cfg["unknown"]) == ("0", "0", "0"), cfg
        _, rows = dump(vm)
        assert screen_has(rows, "Overseer Office"), rows
        assert screen_has(rows, "SETTINGS: OK"), rows
        assert not events(vm, "panic")


def test_trusted_scheme_wins_over_cfg(board, chain1_image, uos_image, need_mtools, tmp_path):
    """The boot info's scheme, amber from chain1, stays; UOS.CFG asks for blue."""
    from uos import after_chain1
    text = b"[system]\nscheme = blue\n"
    with after_chain1(chain1_image, uos_image, board, uos_card(tmp_path, cfg=text)) as vm:
        wait_ready(vm)
        cfg = one(vm, "config")
        assert (cfg["scheme"], cfg["console"]) == ("blue", "1"), cfg
