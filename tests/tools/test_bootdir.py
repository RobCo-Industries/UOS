"""make bootdir writes the files of the chain card into a folder.

Built from stand-in stage files with FIRMWARE=0, so no stage is compiled
and nothing is downloaded. The folder must hold the files of the
`make sdimage CHAIN=1` image made from the same inputs, plus UOSBOOT.LST.
"""
import os
import subprocess

import pytest

from test_chain_card import ROOT, stand_ins  # noqa: F401
from uos import _mtools

LIST = "UOSBOOT.LST"


def make(build, files, *args):
    env = {"PATH": os.environ.get("PATH", ""), "HOME": os.environ.get("HOME", "")}
    return subprocess.run(["make", "-s", "-C", ROOT, "CI=1", f"BUILD={build}",
                           "-o", str(files["UOS.SYS"]), *args], env=env,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)


def bootdir(build, files, dest, *extra, ok=True):
    r = make(build, files, "bootdir", f"DIR={dest}", "FIRMWARE=0",
             f"RETROS_IMG={files['RETROS.IMG']}", f"MFBOOT_SYS={files['MFBOOT.SYS']}", *extra)
    assert (r.returncode == 0) == ok, r.stderr
    return r


def tree(root):
    out = {}
    for top, _, names in os.walk(root):
        for n in names:
            p = os.path.join(top, n)
            out[os.path.relpath(p, root).replace(os.sep, "/")] = open(p, "rb").read()
    return out


def card_tree(sd, tmp_path):
    out = tmp_path / "from_sd"
    out.mkdir()
    _mtools(sd, "mcopy", "-s", "-n", "::/*", os.fspath(out))
    return tree(out)


def test_bootdir_equals_the_chain_card(stand_ins, tmp_path):  # noqa: F811
    build, files = stand_ins
    dest = tmp_path / "card"
    extra = (f"MAINT_SYS={files['MAINT.SYS']}", f"DIAG_SYS={files['DIAG.SYS']}",
             f"BOOT_CFG={files['BOOT.CFG']}")
    bootdir(build, files, dest, *extra)
    got = tree(dest)
    assert got.pop(LIST).decode().split() == sorted(got)
    for name in ("RETROS.IMG", "config.txt", "ROBCO/MFBOOT.SYS", "ROBCO/UOS.SYS",
                 "ROBCO/MAINT.SYS", "ROBCO/DIAG.SYS", "ROBCO/BOOT.CFG", "UOS/SYSUAF.DAT"):
        assert name in got, name
    for name in ("MAINT.SYS", "DIAG.SYS", "MFBOOT.SYS", "UOS.SYS", "BOOT.CFG"):
        assert got["ROBCO/" + name] == files[name].read_bytes(), name
    assert got["RETROS.IMG"] == files["RETROS.IMG"].read_bytes()
    assert b"kernel=RETROS.IMG" in got["config.txt"]
    for name in ("bootcode.bin", "start.elf", "fixup.dat"):
        assert name not in got

    r = make(build, files, "sdimage", "CHAIN=1", f"RETROS_IMG={files['RETROS.IMG']}",
             f"MFBOOT_SYS={files['MFBOOT.SYS']}", *extra)
    assert r.returncode == 0, r.stderr
    assert card_tree(build / "sd.img", tmp_path) == got


@pytest.mark.parametrize("drop", ["RETROS_IMG", "MFBOOT_SYS"])
def test_bootdir_needs_the_chain(stand_ins, tmp_path, drop):  # noqa: F811
    build, files = stand_ins
    dest = tmp_path / "card"
    args = {"RETROS_IMG": files["RETROS.IMG"], "MFBOOT_SYS": files["MFBOOT.SYS"]}
    del args[drop]
    r = make(build, files, "bootdir", f"DIR={dest}", "FIRMWARE=0",
             *(f"{k}={v}" for k, v in args.items()))
    assert r.returncode != 0
    assert f"needs {drop}=" in r.stderr
    assert not dest.exists()


def test_bootdir_rerun_keeps_foreign_files_and_clean_removes_old_ones(stand_ins, tmp_path):  # noqa: F811
    build, files = stand_ins
    dest = tmp_path / "card"
    bootdir(build, files, dest, f"MAINT_SYS={files['MAINT.SYS']}")
    first = tree(dest)

    # The same run again succeeds and changes nothing.
    bootdir(build, files, dest, f"MAINT_SYS={files['MAINT.SYS']}")
    assert tree(dest) == first

    # Without MAINT_SYS the old MAINT.SYS is stale: stop, then CLEAN=1.
    r = bootdir(build, files, dest, ok=False)
    assert "ROBCO/MAINT.SYS" in r.stderr and "CLEAN=1" in r.stderr
    assert tree(dest) == first
    bootdir(build, files, dest, "CLEAN=1")
    assert "ROBCO/MAINT.SYS" not in tree(dest)

    # A file no run wrote stops the run and stays.
    (dest / "NOTES.TXT").write_text("mine\n")
    r = bootdir(build, files, dest, "CLEAN=1", ok=False)
    assert "NOTES.TXT" in r.stderr
    assert (dest / "NOTES.TXT").read_text() == "mine\n"


def test_bootdir_refuses_a_folder_it_did_not_make(stand_ins, tmp_path):  # noqa: F811
    build, files = stand_ins
    dest = tmp_path / "card"
    dest.mkdir()
    (dest / "kernel.img").write_bytes(b"other")
    r = bootdir(build, files, dest, ok=False)
    assert "kernel.img" in r.stderr
    assert sorted(os.listdir(dest)) == ["kernel.img"]
