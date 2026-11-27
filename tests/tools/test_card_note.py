"""CARD_NOTE=<file> puts that file on the card as /ROBCO/VERSIONS.TXT.

Built from the stand-in stage files of test_chain_card, so no stage is
compiled. bootdir runs with FIRMWARE=0, so nothing is downloaded.
"""
import os
import subprocess

from test_bootdir import LIST, bootdir, tree
from test_chain_card import ROOT, read, stand_ins  # noqa: F401
from uos import card_names

NOTE_BYTES = b"UOS 0.1.0 0123456789abcdef\r\nMFBootAgent 0.1.0 fedcba9876543210\r\n"


def write_note(folder, name="note.txt"):
    note = folder / name
    note.write_bytes(NOTE_BYTES)
    return note


def sdimage(build, files, *extra, chain=True):
    env = {"PATH": os.environ.get("PATH", ""), "HOME": os.environ.get("HOME", "")}
    args = ["make", "-s", "-C", ROOT, "CI=1", f"BUILD={build}", "-o", str(files["UOS.SYS"]),
            "sdimage"]
    if chain:
        args += ["CHAIN=1", f"RETROS_IMG={files['RETROS.IMG']}",
                 f"MFBOOT_SYS={files['MFBOOT.SYS']}"]
    return subprocess.run(args + list(extra), env=env, stdout=subprocess.DEVNULL,
                          stderr=subprocess.PIPE, text=True)


def test_chain_card_holds_the_note_only_when_asked(stand_ins, tmp_path):  # noqa: F811
    build, files = stand_ins
    r = sdimage(build, files, f"CARD_NOTE={write_note(tmp_path)}")
    assert r.returncode == 0, r.stderr
    assert "VERSIONS.TXT" in card_names(build / "sd.img", "ROBCO")
    assert read(build / "sd.img", "ROBCO/VERSIONS.TXT", tmp_path) == NOTE_BYTES
    r = sdimage(build, files)
    assert r.returncode == 0, r.stderr
    assert "VERSIONS.TXT" not in card_names(build / "sd.img", "ROBCO")


def test_card_without_the_chain_holds_the_note(stand_ins, tmp_path):  # noqa: F811
    build, files = stand_ins
    r = sdimage(build, files, f"CARD_NOTE={write_note(tmp_path)}", chain=False)
    assert r.returncode == 0, r.stderr
    assert read(build / "sd.img", "ROBCO/VERSIONS.TXT", tmp_path) == NOTE_BYTES


def test_missing_card_note_stops_the_build(stand_ins, tmp_path):  # noqa: F811
    build, files = stand_ins
    r = sdimage(build, files, f"CARD_NOTE={tmp_path / 'none.txt'}")
    assert r.returncode != 0
    assert "CARD_NOTE names no file" in r.stderr


def test_card_note_path_may_hold_a_space_a_quote_and_a_dollar(stand_ins, tmp_path):  # noqa: F811
    build, files = stand_ins
    note = write_note(tmp_path, "Bob's $NOTE.txt")
    r = sdimage(build, files, f"CARD_NOTE={note}")
    assert r.returncode == 0, r.stderr
    assert read(build / "sd.img", "ROBCO/VERSIONS.TXT", tmp_path) == NOTE_BYTES


def test_bootdir_writes_the_note(stand_ins, tmp_path):  # noqa: F811
    build, files = stand_ins
    dest = tmp_path / "card"
    bootdir(build, files, dest, f"CARD_NOTE={write_note(tmp_path)}")
    got = tree(dest)
    assert got["ROBCO/VERSIONS.TXT"] == NOTE_BYTES
    assert "ROBCO/VERSIONS.TXT" in got[LIST].decode()
    # A later run without the note finds a file of an earlier run.
    r = bootdir(build, files, dest, ok=False)
    assert "VERSIONS.TXT" in r.stderr
    bootdir(build, files, dest, "CLEAN=1")
    assert not (dest / "ROBCO" / "VERSIONS.TXT").exists()
