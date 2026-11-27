"""make sdimage CHAIN=1 puts every file of the chain where the boot agent looks.

The card is built from stand-in files, so no stage is compiled. Only the
first stage is a real rimg, since mkbootdir.sh checks it. `-o` tells
make to take the stand-in UOS.SYS as up to date. The names are read back
with card_names, the helper the QEMU tests use.
"""
import os
import subprocess
import sys

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tests"))
sys.path.insert(0, os.path.join(ROOT, "external", "robco-core", "tests"))
sys.path.insert(0, os.path.join(ROOT, "external", "robco-core", "tools"))
import rimg  # noqa: E402
from uos import _mtools, card_names  # noqa: E402

BOOT_CFG = "[boot]\ndefault = UOS\ntimeout = 3\n\n[entry UOS]\npath = /ROBCO/UOS.SYS\n" \
           "\n[entry MAINT]\npath = /ROBCO/MAINT.SYS\n\n[entry DIAG]\npath = /ROBCO/DIAG.SYS\n"


@pytest.fixture
def stand_ins(tmp_path, need_mtools):
    build = tmp_path / "build"
    build.mkdir()
    files = {"UOS.SYS": build / "UOS.SYS", "RETROS.IMG": tmp_path / "retros.img",
             "MFBOOT.SYS": tmp_path / "mfboot.img", "MAINT.SYS": tmp_path / "maint.sys",
             "DIAG.SYS": tmp_path / "diag.sys", "BOOT.CFG": tmp_path / "BOOT.CFG"}
    for name, path in files.items():
        path.write_bytes(BOOT_CFG.encode() if name == "BOOT.CFG" else name.encode() * 64)
    # mkbootdir.sh checks the first stage: it must be a sealed rimg.
    files["RETROS.IMG"].write_bytes(rimg.pack(b"\0" * 64, "BOOT", 5, 0x8000))
    return build, files


def sdimage(build, files, *extra):
    env = {"PATH": os.environ.get("PATH", ""), "HOME": os.environ.get("HOME", "")}
    r = subprocess.run(["make", "-s", "-C", ROOT, "CI=1", f"BUILD={build}",
                        "-o", str(files["UOS.SYS"]), "sdimage", "CHAIN=1",
                        f"RETROS_IMG={files['RETROS.IMG']}", f"MFBOOT_SYS={files['MFBOOT.SYS']}",
                        *extra], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                       text=True)
    assert r.returncode == 0, r.stderr
    return build / "sd.img"


def read(sd, name, tmp_path):
    out = tmp_path / "read.bin"
    _mtools(sd, "mcopy", "-o", "::/" + name, os.fspath(out))
    return out.read_bytes()


def test_chain_card_holds_maint_diag_and_the_given_boot_cfg(stand_ins, tmp_path):
    build, files = stand_ins
    sd = sdimage(build, files, f"MAINT_SYS={files['MAINT.SYS']}",
                 f"DIAG_SYS={files['DIAG.SYS']}", f"BOOT_CFG={files['BOOT.CFG']}")
    assert "RETROS.IMG" in card_names(sd, "")
    assert {"BOOT.CFG", "DIAG.SYS", "MAINT.SYS", "MFBOOT.SYS", "UOS.SYS"} <= \
        set(card_names(sd, "ROBCO"))
    for name in ("MAINT.SYS", "DIAG.SYS", "MFBOOT.SYS", "UOS.SYS", "BOOT.CFG"):
        assert read(sd, "ROBCO/" + name, tmp_path) == files[name].read_bytes(), name
    assert read(sd, "RETROS.IMG", tmp_path) == files["RETROS.IMG"].read_bytes()


def test_chain_card_without_them_keeps_the_one_entry_boot_cfg(stand_ins, tmp_path):
    build, files = stand_ins
    sd = sdimage(build, files)
    names = card_names(sd, "ROBCO")
    assert "MAINT.SYS" not in names and "DIAG.SYS" not in names
    cfg = read(sd, "ROBCO/BOOT.CFG", tmp_path).decode()
    assert "[entry UOS]" in cfg and "[entry MAINT]" not in cfg
