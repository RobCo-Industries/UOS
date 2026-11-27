"""Tests of tools/mkuaf.py."""

import hashlib
import json
import struct
import subprocess
import sys
import zlib
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools" / "mkuaf.py"
sys.path.insert(0, str(ROOT / "tools"))
import mkuaf  # noqa: E402

GOOD = """
[admin]
password = s3cret
privs = SYSTEM, oper
home = ADM
flags = NOHACK

[GUEST]
password = guest
"""


def run(*args, cwd=None):
    env = {"PATH": "/usr/bin:/bin"}
    return subprocess.run([sys.executable, str(TOOL), *map(str, args)], capture_output=True,
                          text=True, cwd=cwd, env=env)


def build_file(tmp_path, text, *extra):
    ini = tmp_path / "users.ini"
    ini.write_text(text)
    out = tmp_path / "SYSUAF.DAT"
    return run("build", ini, "-o", out, *extra), out


def test_hash_definition():
    salt = bytes(range(16))
    h = hashlib.sha256(salt + b"pw").digest()
    h = hashlib.sha256(h + salt + b"pw").digest()
    assert mkuaf.uaf_hash(salt, "pw", 2) == h
    assert mkuaf.uaf_hash(salt, "pw", 1) == hashlib.sha256(salt + b"pw").digest()


def test_seed_salt():
    want = hashlib.sha256(b"seed\0ADMIN").digest()[:16]
    assert mkuaf.seed_salt("seed", "ADMIN") == want


def test_build_layout_and_seed(tmp_path):
    r, out = build_file(tmp_path, GOOD, "--seed", "x", "--iterations", "5")
    assert r.returncode == 0, r.stderr
    data = out.read_bytes()
    assert len(data) == 32 + 2 * 96
    magic, ver, rs, count, res, it, crc, res2 = struct.unpack("<8sHHHHII8s", data[:32])
    assert (magic, ver, rs, count, res, it, res2) == (b"UOSUAF1\0", 1, 96, 2, 0, 5, bytes(8))
    assert crc == zlib.crc32(data[32:])
    rec = data[32:128]
    assert rec[:16] == b"ADMIN" + bytes(11)
    salt = mkuaf.seed_salt("x", "ADMIN")
    assert rec[16:32] == salt
    assert rec[32:64] == mkuaf.uaf_hash(salt, "s3cret", 5)
    assert struct.unpack("<II", rec[64:72]) == (3, 2)
    assert rec[72:88] == b"ADM" + bytes(13)
    assert data[128 + 72:128 + 88] == b"GUEST" + bytes(11)
    r2, out2 = build_file(tmp_path, GOOD, "--seed", "x", "--iterations", "5")
    assert out2.read_bytes() == data


def test_random_salts_differ(tmp_path):
    _, out = build_file(tmp_path, GOOD)
    a = out.read_bytes()
    _, out = build_file(tmp_path, GOOD)
    b = out.read_bytes()
    assert a[48:64] != b[48:64]
    assert struct.unpack("<I", a[16:20])[0] == 2000


def test_check_lists_without_hashes(tmp_path):
    _, out = build_file(tmp_path, GOOD, "--seed", "x", "--iterations", "3")
    r = run("check", out)
    assert r.returncode == 0
    lines = r.stdout.splitlines()
    assert lines[0] == f"{out}: 2 users, 3 iterations"
    assert lines[1] == "ADMIN            home=ADM      privs=SYSTEM,OPER flags=NOHACK"
    assert lines[2] == "GUEST            home=GUEST    privs=- flags=-"
    h = mkuaf.uaf_hash(mkuaf.seed_salt("x", "ADMIN"), "s3cret", 3).hex()
    assert h not in r.stdout and h[:8] not in r.stdout


@pytest.mark.parametrize("text,msg", [
    ("[A B]\npassword=x\n", "user name must be 1 to 16"),
    ("[ABCDEFGHIJKLMNOPQ]\npassword=x\n", "user name must be 1 to 16"),
    ("[A]\n", "no password"),
    ("[A]\npassword=\n", "password must be 1 to 32"),
    ("[A]\npassword=" + "x" * 33 + "\n", "password must be 1 to 32"),
    ("[A]\npassword=x\nprivs=ROOT\n", "unknown privilege 'ROOT'"),
    ("[A]\npassword=x\nflags=SLEEPY\n", "unknown flag 'SLEEPY'"),
    ("[A]\npassword=x\nhome=TOOLONGHOME\n", "home must be 1 to 8"),
    ("[A]\npassword=x\nhome=A.B\n", "home must be 1 to 8"),
    ("[A]\npassword=x\ncolour=red\n", "unknown key 'colour'"),
    ("[A]\npassword=x\nprivs=0x100000000\n", "fit 32 bits"),
    ("[A]\npassword=x\n[a]\npassword=y\n", "user appears twice"),
    ("[A]\npassword=x\n[A]\npassword=y\n", "bad INI"),
    ("password=x\n", "bad INI"),
    ("[A]\npassword=x\n[A1]\npassword=x\n" + "".join(f"[U{i}]\npassword=x\n" for i in range(31)),
     "33 users, at most 32"),
])
def test_build_bad_input(tmp_path, text, msg):
    r, out = build_file(tmp_path, text)
    assert r.returncode == 1
    assert msg in r.stderr
    assert r.stderr.startswith("mkuaf: ")
    assert not out.exists()


def test_password_non_ascii(tmp_path):
    ini = tmp_path / "u.ini"
    ini.write_bytes("[A]\npassword=café\n".encode("utf-8"))
    r = run("build", ini, "-o", tmp_path / "o")
    assert r.returncode == 1 and "not ASCII" in r.stderr


def test_password_control_char():
    with pytest.raises(mkuaf.UafError, match="printable ASCII"):
        mkuaf.check_password("a\x7f")


def test_32_users_ok_and_names_16(tmp_path):
    text = "".join(f"[USER{i:02d}ABCDEFGHI]\npassword=x\nhome=H{i}\n" for i in range(32))
    r, out = build_file(tmp_path, text, "--iterations", "1")
    assert r.returncode == 0, r.stderr
    assert len(out.read_bytes()) == 32 + 32 * 96


@pytest.mark.parametrize("it,ok", [(0, False), (1, True), (100000, True), (100001, False)])
def test_iterations_range(tmp_path, it, ok):
    r, _ = build_file(tmp_path, "[A]\npassword=x\n", "--iterations", it)
    assert (r.returncode == 0) == ok
    if not ok:
        assert "iterations must be 1 to 100000" in r.stderr


def test_missing_input(tmp_path):
    r = run("build", tmp_path / "none.ini", "-o", tmp_path / "o")
    assert r.returncode == 1 and "cannot read" in r.stderr
    r = run("check", tmp_path / "none.dat")
    assert r.returncode == 1 and "cannot read" in r.stderr


def test_usage_errors():
    assert run().returncode == 2
    assert run("frob").returncode == 2
    assert run("build", "x").returncode == 2


def _good_image():
    users = mkuaf.parse_users(GOOD)
    return bytearray(mkuaf.build(users, 3, "s"))


def _fix_crc(d):
    d[20:24] = struct.pack("<I", zlib.crc32(bytes(d[32:])))


@pytest.mark.parametrize("off,val,msg", [
    (0, b"X", "bad magic"),
    (8, b"\x02", "version 2"),
    (10, b"\x5f", "record size 95"),
    (12, b"\x21", "count 33"),
    (14, b"\x01", "header reserved"),
    (31, b"\x01", "header reserved"),
    (16, b"\x00", "iterations 0"),
    (40, b"Z", "CRC mismatch"),
])
def test_check_bad_header(tmp_path, off, val, msg):
    d = _good_image()
    d[off:off + 1] = val
    p = tmp_path / "bad.dat"
    p.write_bytes(bytes(d))
    r = run("check", p)
    assert r.returncode == 1
    assert msg in r.stderr


@pytest.mark.parametrize("off,val,msg", [
    (0, b"\0", "record 0: bad name"),
    (0, b"a", "record 0: bad name"),
    (15, b"X", "record 0: name padding"),
    (72, b"\0", "record 0: bad home"),
    (72 + 9, b"X", "record 0: home padding"),
    (88, b"\x01", "record 0: reserved"),
    (96, b"ADMIN\0", "record 1: user ADMIN appears twice"),
])
def test_check_bad_record(tmp_path, off, val, msg):
    d = _good_image()
    d[32 + off:32 + off + len(val)] = val
    _fix_crc(d)
    p = tmp_path / "bad.dat"
    p.write_bytes(bytes(d))
    r = run("check", p)
    assert r.returncode == 1
    assert msg in r.stderr


def test_check_short_and_size(tmp_path):
    p = tmp_path / "bad.dat"
    p.write_bytes(b"UOSUAF1\0")
    assert "shorter than its header" in run("check", p).stderr
    p.write_bytes(bytes(_good_image()) + b"\0")
    assert "does not match count" in run("check", p).stderr


def test_vectors_file_matches_tool():
    want = json.loads((ROOT / "tests" / "vectors" / "uaf.json").read_text())
    assert want == mkuaf.make_vectors()
    assert len(want) >= 8
    pws = [v["password"] for v in want]
    its = [v["iterations"] for v in want]
    assert any(len(p) == 32 for p in pws) and any(len(p) == 1 for p in pws)
    assert 1 in its and 2000 in its


def test_vectors_header(tmp_path):
    out = tmp_path / "v.h"
    r = run("vectors-h", ROOT / "tests" / "vectors" / "uaf.json", "-o", out)
    assert r.returncode == 0
    text = out.read_text()
    assert '{ "MAX", "!\\"#$%&\'()*+,-./0123456789:;<=>?", "' in text
    bad = tmp_path / "bad.json"
    bad.write_text("{")
    assert run("vectors-h", bad, "-o", out).returncode == 1


def test_default_card_matches_source(tmp_path):
    out = tmp_path / "SYSUAF.DAT"
    r = run("build", ROOT / "tools" / "default_users.ini", "-o", out, "--seed", "uos-default-card")
    assert r.returncode == 0, r.stderr
    assert out.read_bytes() == (ROOT / "card" / "UOS" / "SYSUAF.DAT").read_bytes()


def test_default_card_users():
    iters, users = mkuaf.parse_image((ROOT / "card" / "UOS" / "SYSUAF.DAT").read_bytes())
    assert iters == 2000
    by = {u["name"]: u for u in users}
    assert set(by) == {"ADMIN", "OPER", "GUEST"}
    assert by["ADMIN"]["privs"] == 1 and by["OPER"]["privs"] == 2 and by["GUEST"]["privs"] == 0
    assert by["GUEST"]["flags"] & 2 == 0
    assert by["ADMIN"]["flags"] & 2 and by["OPER"]["flags"] & 2


def test_no_users_ini_on_card():
    assert not list((ROOT / "card").rglob("*.INI")) and not list((ROOT / "card").rglob("*.ini"))


def test_lower_case_name_and_home_build_with_notes(tmp_path):
    r, out = build_file(tmp_path, "[bob]\npassword = pw\nprivs = SYSTEM\nhome = bobby\n",
                        "--seed", "x", "--iterations", "1")
    assert r.returncode == 0, r.stderr
    ini = tmp_path / "users.ini"
    assert f"mkuaf: {ini}: [bob]: note: stored as BOB\n" in r.stderr
    assert f"mkuaf: {ini}: [bob]: note: stored as BOBBY\n" in r.stderr
    _, users = mkuaf.parse_image(out.read_bytes())
    assert users[0]["name"] == "BOB" and users[0]["home"] == "BOBBY"


def test_upper_case_file_has_no_notes(tmp_path):
    r, _ = build_file(tmp_path, "[BOB]\npassword = pw\nprivs = SECURITY\nhome = BOBBY\n",
                      "--iterations", "1")
    assert r.returncode == 0 and r.stderr == ""


@pytest.mark.parametrize("text", [
    "[GUEST]\npassword = pw\nprivs = OPER\n",
    "[ADMIN]\npassword = pw\nprivs = SYSTEM\nflags = DISABLED\n",
])
def test_no_enabled_administrator_builds_with_note(tmp_path, text):
    r, out = build_file(tmp_path, text, "--iterations", "1")
    assert r.returncode == 0, r.stderr
    assert r.stderr == (f"mkuaf: {tmp_path / 'users.ini'}: note: no enabled user holds "
                        "SYSTEM or SECURITY, nobody can run AUTHORIZE\n")
    assert out.is_file()
