"""make bootdir writes the files of the chain card into a folder.

Built from stand-in stage files with FIRMWARE=0, so no stage is compiled
and nothing is downloaded. The folder must hold the files of the
`make sdimage CHAIN=1` image made from the same inputs, plus UOSBOOT.LST.
"""
import hashlib
import os
import shutil
import sys
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
    lines = got.pop(LIST).decode().splitlines()
    assert [line.split("  ", 1)[1] for line in lines] == sorted(got)
    for line in lines:
        h, name = line.split("  ", 1)
        assert h == hashlib.sha256(got[name]).hexdigest(), name
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


def test_bootdir_dir_with_a_space(stand_ins, tmp_path):  # noqa: F811
    build, files = stand_ins
    dest = tmp_path / "UOS CARD"
    bootdir(build, files, dest)
    got = tree(dest)
    assert got["ROBCO/UOS.SYS"] == files["UOS.SYS"].read_bytes()
    assert LIST in got
    assert sorted(os.listdir(tmp_path)).count("UOS") == 0


def listed(dest):
    return {line.split("  ", 1)[1] for line in (dest / LIST).read_text().splitlines()}


def test_bootdir_rerun_updates_a_card_in_use(stand_ins, tmp_path):  # noqa: F811
    build, files = stand_ins
    dest = tmp_path / "card"
    bootdir(build, files, dest)
    db = dest / "UOS" / "SYSUAF.DAT"
    shipped_db = db.read_bytes()

    # The terminal changed its user database and saved a home file.
    db.write_bytes(b"changed passwords")
    saved = dest / "UOS" / "HOME" / "GUEST" / "SAVED.TXT"
    saved.write_text("mine\n")
    # A new UOS.SYS: nobody touched the card's copy.
    files["UOS.SYS"].write_bytes(b"NEW UOS" * 64)

    r = bootdir(build, files, dest)
    assert "kept UOS/SYSUAF.DAT: changed since the last run" in r.stdout
    assert db.read_bytes() == b"changed passwords"
    assert (dest / "ROBCO" / "UOS.SYS").read_bytes() == b"NEW UOS" * 64
    assert saved.read_text() == "mine\n"
    assert "SAVED.TXT" not in r.stdout + r.stderr
    assert "UOS/HOME/GUEST/SAVED.TXT" not in listed(dest)

    # The next run keeps it again: the list kept the old record.
    r = bootdir(build, files, dest)
    assert "kept UOS/SYSUAF.DAT" in r.stdout

    # REPLACE=1 brings the shipped file back and leaves the home file.
    r = bootdir(build, files, dest, "REPLACE=1")
    assert db.read_bytes() == shipped_db
    assert saved.read_text() == "mine\n"


def test_bootdir_clash_stops_and_changes_nothing(stand_ins, tmp_path):  # noqa: F811
    build, files = stand_ins
    dest = tmp_path / "card"
    bootdir(build, files, dest)
    (dest / "ROBCO" / "MAINT.SYS").write_bytes(b"by hand")
    (dest / "UOS" / "SYSUAF.DAT").write_bytes(b"changed")
    before = tree(dest)
    r = bootdir(build, files, dest, f"MAINT_SYS={files['MAINT.SYS']}", "REPLACE=1", ok=False)
    assert "ROBCO/MAINT.SYS" in r.stderr
    assert tree(dest) == before


@pytest.mark.parametrize("kind", ["file", "folder"])
def test_bootdir_file_and_folder_clash_changes_nothing(stand_ins, tmp_path, kind):  # noqa: F811
    build, files = stand_ins
    dest = tmp_path / "card"
    bootdir(build, files, dest)
    # New bytes for a file the run writes before it reaches the clash.
    files["UOS.SYS"].write_bytes(b"NEW UOS" * 64)
    if kind == "file":
        shutil.rmtree(dest / "UOS")
        (dest / "UOS").write_bytes(b"a file")
        name = "UOS: a file where a folder is needed"
    else:
        (dest / "UOS" / "SYSUAF.DAT").unlink()
        (dest / "UOS" / "SYSUAF.DAT").mkdir()
        name = "UOS/SYSUAF.DAT: a folder where a file goes"
    before = tree(dest)
    r = bootdir(build, files, dest, "REPLACE=1", ok=False)
    assert name in r.stderr
    assert tree(dest) == before


def test_bootdir_stale_files_and_clean(stand_ins, tmp_path):  # noqa: F811
    build, files = stand_ins
    dest = tmp_path / "card"
    bootdir(build, files, dest, f"MAINT_SYS={files['MAINT.SYS']}", f"DIAG_SYS={files['DIAG.SYS']}")
    (dest / "ROBCO" / "DIAG.SYS").write_bytes(b"changed")
    before = tree(dest)

    # Without MAINT_SYS and DIAG_SYS: the unchanged one stops the run,
    # the changed one is not named as stale. Nothing changes.
    r = bootdir(build, files, dest, ok=False)
    assert "ROBCO/MAINT.SYS" in r.stderr and "CLEAN=1" in r.stderr
    assert "ROBCO/DIAG.SYS" not in r.stderr
    assert tree(dest) == before

    # CLEAN=1 removes the unchanged one and keeps the changed one.
    r = bootdir(build, files, dest, "CLEAN=1")
    assert not (dest / "ROBCO" / "MAINT.SYS").exists()
    assert (dest / "ROBCO" / "DIAG.SYS").read_bytes() == b"changed"
    assert "kept ROBCO/DIAG.SYS: changed since the last run" in r.stdout
    assert (dest / "UOS" / "HOME" / "GUEST").is_dir()
    assert "ROBCO/DIAG.SYS" in listed(dest)
    assert "ROBCO/MAINT.SYS" not in listed(dest)


def test_bootdir_changed_stale_file_keeps_its_record(stand_ins, tmp_path):  # noqa: F811
    build, files = stand_ins
    dest = tmp_path / "card"
    bootdir(build, files, dest, f"DIAG_SYS={files['DIAG.SYS']}")
    (dest / "ROBCO" / "DIAG.SYS").write_bytes(b"changed")

    # Gone from the stage and changed: kept and named, no CLEAN=1 needed.
    r = bootdir(build, files, dest)
    assert "kept ROBCO/DIAG.SYS: changed since the last run" in r.stdout
    assert "ROBCO/DIAG.SYS" in listed(dest)

    # Back in the stage: a changed listed file, kept, then REPLACE=1 writes it.
    r = bootdir(build, files, dest, f"DIAG_SYS={files['DIAG.SYS']}")
    assert (dest / "ROBCO" / "DIAG.SYS").read_bytes() == b"changed"
    bootdir(build, files, dest, f"DIAG_SYS={files['DIAG.SYS']}", "REPLACE=1")
    assert (dest / "ROBCO" / "DIAG.SYS").read_bytes() == files["DIAG.SYS"].read_bytes()


def test_bootdir_refuses_a_folder_it_did_not_make(stand_ins, tmp_path):  # noqa: F811
    build, files = stand_ins
    dest = tmp_path / "card"
    dest.mkdir()
    (dest / "kernel.img").write_bytes(b"other")
    r = bootdir(build, files, dest, ok=False)
    assert "kernel.img" in r.stderr
    assert sorted(os.listdir(dest)) == ["kernel.img"]


FIRMWARE = ("bootcode.bin", "start.elf", "fixup.dat")


def sync(stage, dest, *args):
    tool = os.path.join(ROOT, "tools", "syncbootdir.py")
    return subprocess.run([sys.executable, "-I", tool, str(stage), str(dest), *args],
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)


def test_firmware_0_leaves_the_firmware_alone(tmp_path):
    """The firmware cannot be fetched offline: the tool runs on staged folders.
    The names come from FIRMWARE_FILES in the Makefile, the list make passes."""
    names = subprocess.run(["make", "-s", "-C", ROOT, "-p", "-n", "-f", "Makefile", "clean"],
                           stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True).stdout
    line = next(x for x in names.splitlines() if x.startswith("FIRMWARE_FILES :="))
    assert tuple(line.split(":=", 1)[1].split()) == FIRMWARE
    outside = [arg for n in FIRMWARE for arg in ("--outside", n)]

    full, bare, dest = tmp_path / "full", tmp_path / "bare", tmp_path / "card"
    for stage in (full, bare):
        stage.mkdir()
        (stage / "RETROS.IMG").write_bytes(b"image")
    for n in FIRMWARE:
        (full / n).write_bytes(n.encode())
    assert sync(full, dest).returncode == 0
    before = tree(dest)

    for extra in ([], ["--clean"]):
        r = sync(bare, dest, *outside, *extra)
        assert r.returncode == 0, r.stderr
        assert tree(dest) == before

    # Without the option the firmware is stale, as before.
    r = sync(bare, dest)
    assert r.returncode == 1 and "bootcode.bin" in r.stderr


def test_firmware_0_first_run_and_a_hand_copied_firmware(tmp_path):
    bare, full, dest = tmp_path / "bare", tmp_path / "full", tmp_path / "card"
    for stage in (bare, full):
        stage.mkdir()
        (stage / "RETROS.IMG").write_bytes(b"image")
    for n in FIRMWARE:
        (full / n).write_bytes(n.encode())
    outside = [arg for n in FIRMWARE for arg in ("--outside", n)]
    assert sync(bare, dest, *outside).returncode == 0
    assert "bootcode" not in (dest / LIST).read_text()
    (dest / "start.elf").write_bytes(b"by hand")
    r = sync(full, dest)
    assert r.returncode == 1 and "start.elf" in r.stderr
    assert (dest / "start.elf").read_bytes() == b"by hand"
