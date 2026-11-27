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
    """A space, a quote and a dollar sign reach the tool as they were typed."""
    build, files = stand_ins
    dest = tmp_path / "UOS $CARD's"
    bootdir(build, files, dest)
    assert sorted(os.listdir(tmp_path)).count("UOS $CARD's") == 1
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


def test_bootdir_never_writes_through_a_link(stand_ins, tmp_path):  # noqa: F811
    build, files = stand_ins
    dest, away = tmp_path / "card", tmp_path / "away"
    bootdir(build, files, dest)
    shutil.copytree(dest / "ROBCO", away)
    shutil.rmtree(dest / "ROBCO")
    (dest / "ROBCO").symlink_to(away)
    files["UOS.SYS"].write_bytes(b"NEW UOS" * 64)
    before, outside = tree(dest), tree(away)
    r = bootdir(build, files, dest, ok=False)
    assert "ROBCO: a symbolic link" in r.stderr
    assert tree(away) == outside and tree(dest) == before


def test_bootdir_never_removes_through_a_link(stand_ins, tmp_path):  # noqa: F811
    build, files = stand_ins
    dest, away = tmp_path / "card", tmp_path / "away.sys"
    bootdir(build, files, dest, f"MAINT_SYS={files['MAINT.SYS']}")
    maint = dest / "ROBCO" / "MAINT.SYS"
    away.write_bytes(maint.read_bytes())
    maint.unlink()
    maint.symlink_to(away)
    r = bootdir(build, files, dest, "CLEAN=1", ok=False)
    assert "ROBCO/MAINT.SYS: a symbolic link" in r.stderr
    assert away.exists() and maint.is_symlink()


def test_bootdir_dir_may_be_a_link(stand_ins, tmp_path):  # noqa: F811
    build, files = stand_ins
    real, link = tmp_path / "real", tmp_path / "card"
    real.mkdir()
    link.symlink_to(real)
    bootdir(build, files, link)
    bootdir(build, files, link)
    assert link.is_symlink() and (real / LIST).is_file()


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


def stage_of(root, files):
    root.mkdir(exist_ok=True)
    for n, data in files.items():
        (root / n).parent.mkdir(parents=True, exist_ok=True)
        (root / n).write_bytes(data)
    return root


def case_matters(folder):
    """True when folder tells A from a, as a FAT32 card does not."""
    (folder / "CASE.PRB").write_bytes(b"")
    try:
        return not (folder / "case.prb").exists()
    finally:
        (folder / "CASE.PRB").unlink()


def test_a_case_renamed_and_edited_file_is_kept(tmp_path):
    stage, dest = stage_of(tmp_path / "s", {"FOO.TXT": b"one"}), tmp_path / "card"
    assert sync(stage, dest).returncode == 0
    os.rename(dest / "FOO.TXT", dest / "foo.txt")
    (dest / "foo.txt").write_bytes(b"edited")
    (stage / "FOO.TXT").write_bytes(b"two")
    r = sync(stage, dest)
    assert r.returncode == 0, r.stderr
    assert "kept FOO.TXT: changed since the last run" in r.stdout
    assert sorted(os.listdir(dest)) == [LIST, "foo.txt"]
    assert (dest / "foo.txt").read_bytes() == b"edited"
    assert listed(dest) == {"FOO.TXT"}


def test_a_case_renamed_file_is_replaced_through_its_entry(tmp_path):
    stage = stage_of(tmp_path / "s", {"FOO.TXT": b"one", "SUB/X.BIN": b"x"})
    dest = tmp_path / "card"
    assert sync(stage, dest).returncode == 0
    os.rename(dest / "FOO.TXT", dest / "foo.txt")
    os.rename(dest / "SUB", dest / "sub")
    os.rename(dest / "sub" / "X.BIN", dest / "sub" / "x.bin")
    stage_of(stage, {"FOO.TXT": b"two", "SUB/X.BIN": b"y"})
    r = sync(stage, dest)
    assert r.returncode == 0, r.stderr
    # One entry each: the file takes the stage's spelling, the folder keeps its own.
    assert sorted(os.listdir(dest)) == ["FOO.TXT", LIST, "sub"]
    assert os.listdir(dest / "sub") == ["X.BIN"]
    assert (dest / "FOO.TXT").read_bytes() == b"two"
    assert (dest / "sub" / "X.BIN").read_bytes() == b"y"
    assert listed(dest) == {"FOO.TXT", "SUB/X.BIN"}
    assert len((dest / LIST).read_text().splitlines()) == 2


def test_a_case_clash_and_a_case_link_stop_the_run(tmp_path):
    stage = stage_of(tmp_path / "s", {"A.TXT": b"a", "ROBCO/B.SYS": b"b"})
    dest = tmp_path / "card"
    assert sync(stage, dest).returncode == 0
    (dest / "foo.txt").write_bytes(b"by hand")
    stage_of(stage, {"FOO.TXT": b"shipped"})
    r = sync(stage, dest)
    assert r.returncode == 1 and "foo.txt" in r.stderr
    assert (dest / "foo.txt").read_bytes() == b"by hand"
    (dest / "foo.txt").unlink()
    shutil.move(dest / "ROBCO", tmp_path / "away")
    (dest / "robco").symlink_to(tmp_path / "away")
    r = sync(stage, dest)
    assert r.returncode == 1 and "ROBCO: a symbolic link" in r.stderr
    assert not (dest / "FOO.TXT").exists()


@pytest.mark.parametrize("where", ["stage", "dir"])
def test_names_that_differ_only_by_case_stop_the_run(tmp_path, where):
    stage, dest = stage_of(tmp_path / "s", {"A.TXT": b"a"}), tmp_path / "card"
    assert sync(stage, dest).returncode == 0
    if not case_matters(tmp_path):
        pytest.skip("this file system cannot hold two names that differ only by case")
    (stage if where == "stage" else dest).joinpath("a.txt").write_bytes(b"twin")
    (stage / "NEW.TXT").write_bytes(b"new")
    r = sync(stage, dest)
    assert r.returncode == 1 and "A.TXT" in r.stderr and "a.txt" in r.stderr
    assert not (dest / "NEW.TXT").exists()


def test_a_failed_copy_damages_nothing_and_the_next_run_finishes(tmp_path, monkeypatch):
    import errno
    import importlib.util
    spec = importlib.util.spec_from_file_location(
        "syncbootdir", os.path.join(ROOT, "tools", "syncbootdir.py"))
    tool = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(tool)

    names = ["A.SYS", "B.SYS", "C.SYS", "SUB/D.SYS"]
    stage = stage_of(tmp_path / "s", {n: b"old " + n.encode() for n in names})
    dest = tmp_path / "card"
    assert sync(stage, dest).returncode == 0
    stage_of(stage, {n: b"new " + n.encode() * 100 for n in names})
    old = tree(dest)

    calls = []
    real = shutil.copyfileobj

    def full_card(i, o):
        calls.append(1)
        if len(calls) == 3:
            o.write(i.read(5))
            raise OSError(errno.ENOSPC, "card full")
        real(i, o)

    monkeypatch.setattr(tool.shutil, "copyfileobj", full_card)
    monkeypatch.setattr(sys, "argv", ["syncbootdir.py", str(stage), str(dest)])
    with pytest.raises(SystemExit) as stopped:
        tool.main()
    assert stopped.value.code == 1
    monkeypatch.undo()
    got = tree(dest)
    assert got["A.SYS"] == (stage / "A.SYS").read_bytes()
    assert got["B.SYS"] == (stage / "B.SYS").read_bytes()
    assert got["C.SYS"] == old["C.SYS"] and got["SUB/D.SYS"] == old["SUB/D.SYS"]
    assert got[LIST] == old[LIST]
    assert tool.TMP not in got
    # A temporary file left by a run that lost power goes too.
    (dest / "SUB" / tool.TMP).write_bytes(b"half")

    r = sync(stage, dest)
    assert r.returncode == 0, r.stderr
    assert "kept" not in r.stdout
    got = tree(dest)
    assert all(not n.endswith(tool.TMP) for n in got)
    for line in got.pop(LIST).decode().splitlines():
        h, n = line.split("  ", 1)
        assert h == hashlib.sha256(got.pop(n)).hexdigest() == \
            hashlib.sha256((stage / n).read_bytes()).hexdigest(), n
    assert got == {}


def test_guard_refuses_a_dir_on_a_staging_folder(tmp_path):
    stage, other = tmp_path / "build" / "sdstage", tmp_path / "card"
    (tmp_path / "link").symlink_to(tmp_path / "build")
    for dest in (stage, stage / "sub", tmp_path / "build", tmp_path / "link" / "sdstage"):
        r = sync("guard", dest, stage, tmp_path / "build" / "bootstage")
        assert r.returncode == 1 and f"refusing DIR {dest}" in r.stderr, dest
    assert sync("guard", other, stage).returncode == 0
    assert sync("guard", tmp_path / "builds", stage).returncode == 0


@pytest.mark.parametrize("folder", ["sdstage", "bootstage"])
def test_bootdir_refuses_a_staging_folder_and_clears_nothing(stand_ins, folder):  # noqa: F811
    build, files = stand_ins
    dest = build / folder
    dest.mkdir()
    (dest / "MINE.TXT").write_bytes(b"keep")
    r = bootdir(build, files, dest, ok=False)
    assert f"refusing DIR {dest}" in r.stderr
    assert (dest / "MINE.TXT").read_bytes() == b"keep"


def test_a_folder_with_the_temporary_name_stops_before_any_write(tmp_path):
    """The run would fail at the file beside that folder, with earlier
    files already replaced. It stops first and changes nothing."""
    stage, dest = tmp_path / "stage", tmp_path / "card"
    (stage / "SUB").mkdir(parents=True)
    (stage / "A.SYS").write_bytes(b"a1")
    (stage / "SUB" / "B.SYS").write_bytes(b"b1")
    assert sync(stage, dest).returncode == 0
    before = tree(dest)
    (stage / "A.SYS").write_bytes(b"a2")
    (stage / "SUB" / "B.SYS").write_bytes(b"b2")
    (dest / "SUB" / "UOSBOOT.TMP").mkdir()
    r = sync(stage, dest)
    assert r.returncode != 0
    assert "SUB/UOSBOOT.TMP: a folder with the temporary name" in r.stderr
    assert tree(dest) == before
    # A folder of that name where the run writes nothing is not in the way.
    (dest / "SUB" / "UOSBOOT.TMP").rmdir()
    (dest / "MINE").mkdir()
    (dest / "MINE" / "UOSBOOT.TMP").mkdir()
    assert sync(stage, dest).returncode == 0
    assert tree(dest)["SUB/B.SYS"] == b"b2"


def test_a_stopped_first_run_and_a_stopped_new_file_are_finished(tmp_path):
    """Before the list is written, a stopped run leaves stage files with
    the stage's bytes. The next run takes them as written and goes on."""
    stage, dest = tmp_path / "stage", tmp_path / "card"
    (stage / "SUB").mkdir(parents=True)
    (stage / "A.SYS").write_bytes(b"a1")
    (stage / "SUB" / "B.SYS").write_bytes(b"b1")
    # A first run that stopped after one file: no list yet.
    dest.mkdir()
    (dest / "A.SYS").write_bytes(b"a1")
    r = sync(stage, dest)
    assert r.returncode == 0, r.stderr
    assert "kept" not in r.stdout
    assert tree(dest)["SUB/B.SYS"] == b"b1" and LIST in tree(dest)
    # An update that adds a file and stopped after it: the list is old.
    (stage / "C.SYS").write_bytes(b"c1")
    (dest / "C.SYS").write_bytes(b"c1")
    r = sync(stage, dest)
    assert r.returncode == 0, r.stderr
    assert "C.SYS" in (dest / LIST).read_text()
    # Other bytes under a stage name, with no record, are still a clash.
    (stage / "D.SYS").write_bytes(b"d1")
    (dest / "D.SYS").write_bytes(b"mine")
    before = tree(dest)
    r = sync(stage, dest)
    assert r.returncode != 0 and "D.SYS" in r.stderr
    assert tree(dest) == before
    # And a folder with a stranger and no list is still refused.
    other = tmp_path / "other"
    other.mkdir()
    (other / "A.SYS").write_bytes(b"a1")
    (other / "kernel.img").write_bytes(b"x")
    r = sync(stage, other)
    assert r.returncode != 0 and "kernel.img" in r.stderr
    assert sorted(os.listdir(other)) == ["A.SYS", "kernel.img"]
