"""tools/mkholo.py: building, listing, checking, every bad input, and agreement with the C validator.

Needs ROBCO_CORE (RobCo-Core checkout) and HOLO_CHECK (the holocheck host
tool from tests/host/holo.mk) in the environment.
"""

import os
import struct
import subprocess
import sys

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import mkholo  # noqa: E402

CORE = os.environ.get("ROBCO_CORE", "")
RIMG = os.path.join(CORE, "tools", "rimg.py")
CHECK = os.environ.get("HOLO_CHECK", os.path.join(ROOT, "build", "holo", "holocheck"))
CARD = os.path.join(ROOT, "card", "UOS", "HOLO")
SAMPLES = {
    "TECHLOG": "Alder Creek technician's log",
    "PROCS": "Alder Creek operating procedures",
    "STORY": "The rake with the longer handle",
}


@pytest.fixture(scope="module")
def rimg():
    assert os.path.isfile(RIMG), "set ROBCO_CORE to a RobCo-Core checkout"
    return mkholo.load_rimg(RIMG)


def run(*args):
    return subprocess.run([sys.executable, os.path.join(ROOT, "tools", "mkholo.py"), *args],
                          capture_output=True, text=True)


def write(folder, files):
    folder.mkdir(exist_ok=True)
    for name, data in files.items():
        (folder / name).write_bytes(data if isinstance(data, bytes) else data.encode())
    return folder


def build(tmp_path, files, title="A tape", out="T.HTP"):
    src = write(tmp_path / "src", files)
    return run("build", "--rimg", RIMG, "--title", title, "--out", str(tmp_path / out), str(src))


def holocheck(paths):
    assert os.access(CHECK, os.X_OK), f"build {CHECK} first"
    r = subprocess.run([CHECK, *paths], capture_output=True, text=True)
    return [line.rsplit(" ", 1)[1] for line in r.stdout.splitlines()]


def test_build_list_check(tmp_path):
    r = build(tmp_path, {"B.TXT": "Second\nbody two\n", "A.TXT": "First\r\nbody one\r\n"})
    assert r.returncode == 0, r.stderr
    tape = str(tmp_path / "T.HTP")
    r = run("list", "--rimg", RIMG, tape)
    assert r.returncode == 0
    assert r.stdout == ("title: A tape\n01  First  (10 bytes)\n02  Second  (9 bytes)\n")
    r = run("check", "--rimg", RIMG, tape)
    assert r.returncode == 0 and r.stdout == f"{tape}: ok\n"
    assert holocheck([tape]) == ["ok"]


def test_layout_bytes(tmp_path, rimg):
    assert build(tmp_path, {"A.TXT": "One\nxy"}).returncode == 0
    data = (tmp_path / "T.HTP").read_bytes()
    assert struct.unpack_from("<I", data, 0)[0] == 0xEAFFFFFE
    assert not any(data[4:0x20])
    h = rimg.parse_header(data)
    assert h["magic"] == b"ROBCO78\0" and h["subtype"] == 2 and h["flags"] == 1
    assert h["load_addr"] == 0 and h["mem_size"] == h["file_size"] == len(data)
    assert h["abi_version"] == 1 << 16 and h["name"].rstrip(b"\0") == b"T"
    assert data[0x60:0x67] == b"A tape\0"
    pl = data[0x120:]
    assert pl[:8] == b"HTXT\x01\x00\x01\x00"
    assert pl[8:40] == b"One".ljust(32, b"\0")
    assert struct.unpack_from("<II", pl, 40) == (48, 2)
    assert pl[48:] == b"xy"


def test_order_file(tmp_path):
    r = build(tmp_path, {"ORDER.TXT": "# comment\n\nZ.TXT Last file first\nA.TXT  Then A\n",
                         "A.TXT": "a body\n", "Z.TXT": "z body\n"})
    assert r.returncode == 0, r.stderr
    r = run("list", "--rimg", RIMG, str(tmp_path / "T.HTP"))
    assert r.stdout.splitlines()[1:] == ["01  Last file first  (7 bytes)",
                                         "02  Then A  (7 bytes)"]


@pytest.mark.parametrize("files,msg", [
    ({"ORDER.TXT": "A.TXT\n", "A.TXT": "x"}, "expected FILE.TXT and an entry title"),
    ({"ORDER.TXT": "B.TXT Bee\n", "A.TXT": "x"}, "B.TXT is not a .TXT file"),
    ({"ORDER.TXT": "A.TXT a\nA.TXT b\n", "A.TXT": "x"}, "listed twice"),
    ({"ORDER.TXT": "A.TXT a\n", "A.TXT": "x", "B.TXT": "y"}, "B.TXT is not listed"),
    ({"ORDER.TXT": "A.TXT " + "n" * 32 + "\n", "A.TXT": "x"}, "more than 31"),
    ({"ORDER.TXT": "A.TXT caf\xe9\n".encode("latin-1"), "A.TXT": "x"}, "not plain ASCII"),
    ({"A.TXT": "Title\nbad \xe9 byte".encode("latin-1")}, "byte 0xe9 at offset 10"),
    ({"A.TXT": b"Title\nnul\0"}, "byte 0x00 at offset 9"),
    ({"A.TXT": "\nbody"}, "entry title is empty"),
    ({"A.TXT": "x" * 32 + "\nbody"}, "32 characters, more than 31"),
    ({"A.TXT": "T\n" + "x" * 65535}, None),
    ({"A.TXT": "T\n" + "x" * 65537}, "65537 bytes, more than 65536"),
    ({"README": "not a text file"}, "no .TXT files"),
    ({f"E{i:02d}.TXT": f"E{i}\n" for i in range(33)}, "33 entries, more than 32"),
    ({f"E{i:02d}.TXT": f"E{i}\n" for i in range(32)}, None),
])
def test_bad_folders(tmp_path, files, msg):
    r = build(tmp_path, files)
    if msg is None:
        assert r.returncode == 0, r.stderr
        assert holocheck([str(tmp_path / "T.HTP")]) == ["ok"]
    else:
        assert r.returncode == mkholo.EX_BAD
        assert msg in r.stderr
        assert not (tmp_path / "T.HTP").exists()


def test_entry_at_64k_exactly(tmp_path):
    r = build(tmp_path, {"ORDER.TXT": "A.TXT Big\n", "A.TXT": "x" * 65536})
    assert r.returncode == 0, r.stderr
    assert holocheck([str(tmp_path / "T.HTP")]) == ["ok"]
    r = build(tmp_path, {"ORDER.TXT": "A.TXT Big\n", "A.TXT": "x" * 65537})
    assert r.returncode == 1 and "more than 65536" in r.stderr


@pytest.mark.parametrize("title,msg", [
    ("", "tape title is empty"),
    ("t" * 63, None),
    ("t" * 64, "64 characters, more than 63"),
    ("caf\xe9", "not ASCII"),
    ("tab\there", "not printable"),
])
def test_tape_titles(tmp_path, title, msg):
    r = build(tmp_path, {"A.TXT": "T\nbody"}, title=title)
    if msg is None:
        assert r.returncode == 0
    else:
        assert r.returncode == 1 and msg in r.stderr


def test_usage_and_io_errors(tmp_path):
    assert run().returncode == mkholo.EX_USAGE
    assert run("build", "x").returncode == mkholo.EX_USAGE
    assert run("bogus").returncode == mkholo.EX_USAGE
    r = run("build", "--rimg", RIMG, "--title", "t", "--out", "o", str(tmp_path / "nope"))
    assert r.returncode == mkholo.EX_IO and "not a folder" in r.stderr
    r = run("list", "--rimg", RIMG, str(tmp_path / "nope.HTP"))
    assert r.returncode == mkholo.EX_IO
    r = run("list", "--rimg", str(tmp_path / "rimg.py"), "x")
    assert r.returncode == mkholo.EX_RIMG and "rimg.py not found" in r.stderr
    env = {"PATH": os.environ.get("PATH", "")}
    r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "mkholo.py"), "list", "x"],
                       capture_output=True, text=True, env=env)
    assert r.returncode == mkholo.EX_RIMG and "ROBCO_CORE is not set" in r.stderr
    env["ROBCO_CORE"] = CORE
    (tmp_path / "x").write_bytes(b"")
    r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "mkholo.py"), "list",
                        str(tmp_path / "x")], capture_output=True, text=True, env=env)
    assert r.returncode == 1 and "io: tape file is too short" in r.stderr


def test_check_reports_damage(tmp_path):
    build(tmp_path, {"A.TXT": "T\nbody"})
    tape = tmp_path / "T.HTP"
    data = bytearray(tape.read_bytes())
    data[-1] ^= 0x20
    tape.write_bytes(bytes(data))
    r = run("check", "--rimg", RIMG, str(tape))
    assert r.returncode == 1 and r.stdout == f"{tape}: crc: tape is damaged\n"


def test_every_single_byte_change_is_refused(tmp_path, rimg):
    """The C validator and the Python check agree on every mutated byte, and both refuse it."""
    assert build(tmp_path, {"A.TXT": "One\nab\n", "B.TXT": "Two\ncd"}).returncode == 0
    good = (tmp_path / "T.HTP").read_bytes()
    assert holocheck([str(tmp_path / "T.HTP")]) == ["ok"]
    paths, expect = [], []
    mdir = tmp_path / "m"
    mdir.mkdir()
    for i in range(len(good)):
        for delta in (0x01, 0x80):
            data = bytearray(good)
            data[i] ^= delta
            p = mdir / f"{i}_{delta}.HTP"
            p.write_bytes(bytes(data))
            paths.append(str(p))
            try:
                mkholo.validate(rimg, bytes(data))
                expect.append("ok")
            except mkholo.TapeError as e:
                expect.append(e.name)
    got = holocheck(paths)
    assert len(got) == len(paths)
    assert "ok" not in got
    assert got == expect


def test_c_and_python_agree_on_crafted_errors(tmp_path, rimg):
    """Resealed structural damage: both validators give the same error name."""
    assert build(tmp_path, {"A.TXT": "One\nab\n", "B.TXT": "Two\ncd"}).returncode == 0
    good = bytearray((tmp_path / "T.HTP").read_bytes())
    pl = 0x120
    cases = {
        "subtype": (0x48, struct.pack("<I", 1)),
        "bounds": (0x30, struct.pack("<I", 0x800000)),
        "abi": (0x40, struct.pack("<I", 2 << 16)),
        "title": (0x60, b"\0"),
        "arcmagic": (pl, b"XTXT"),
        "arcver": (pl + 4, struct.pack("<H", 2)),
        "count": (pl + 6, struct.pack("<H", 33)),
        "table": (pl + 6, struct.pack("<H", 3)),
        "name": (pl + 8, b"\x01"),
        "length": (pl + 8 + 36, struct.pack("<I", 65537)),
        "range": (pl + 8 + 32, struct.pack("<I", 0)),
        "overlap": (pl + 48 + 32, struct.pack("<I", 88)),
    }
    paths, names = [], []
    for name, (off, raw) in cases.items():
        data = bytearray(good)
        data[off:off + len(raw)] = raw
        struct.pack_into("<I", data, 0x3C, rimg.image_crc(data, len(data)))
        with pytest.raises(mkholo.TapeError) as e:
            mkholo.validate(rimg, bytes(data))
        assert e.value.name == name
        p = tmp_path / f"{name}.HTP"
        p.write_bytes(bytes(data))
        paths.append(str(p))
        names.append(name)
    assert holocheck(paths) == names


@pytest.mark.parametrize("tape", sorted(SAMPLES))
def test_shipped_tapes_match_sources(tmp_path, tape, rimg):
    shipped = os.path.join(CARD, f"{tape}.HTP")
    out = str(tmp_path / f"{tape}.HTP")
    r = run("build", "--rimg", RIMG, "--title", SAMPLES[tape], "--out", out,
            os.path.join(CARD, "SRC", tape))
    assert r.returncode == 0, r.stderr
    with open(out, "rb") as a, open(shipped, "rb") as b:
        assert a.read() == b.read(), f"{tape}.HTP is stale: rebuild it from SRC/{tape}"
    assert holocheck([shipped]) == ["ok"]


def test_shipped_tapes_are_all_listed():
    found = sorted(n[:-4] for n in os.listdir(CARD) if n.endswith(".HTP"))
    assert found == sorted(SAMPLES)
    total = sum(len(mkholo.validate(mkholo.load_rimg(RIMG),
                                    open(os.path.join(CARD, f"{t}.HTP"), "rb").read())[1])
                for t in SAMPLES)
    assert total == 12
