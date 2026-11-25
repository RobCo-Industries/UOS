"""The vfs service through the `fs` maintenance command: specification forms,
listings, writes, copies, the access table, a full card, the degraded modes
and the DEV: hook."""

import re
import struct
import zlib

from uos import (TIMEOUT, alone, answer, card, card_free, card_names, card_put, card_tree, fs,
                 uos_card, wait_ready)
from harness.boot import parse_markers
from harness.sdimage import make_sd_image

SYS, OPER, SECURITY, READALL = 1, 2, 4, 8
DATE = re.compile(r"^\d\d-(JAN|FEB|MAR|APR|MAY|JUN|JUL|AUG|SEP|OCT|NOV|DEC)-\d{4}$")
TIME = re.compile(r"^\d\d:\d\d:\d\d$")
REBOOTED = re.compile(r"(@RC stage=uos ev=ready [^\n]*\n[\s\S]*){2}")

# One copy step moves at most 16 KiB. The slowest step measured in QEMU took
# 2 ticks on raspi0, raspi1ap and raspi2b and 14 to 25 ticks on raspi3b,
# whose SD model is slower (see docs/TESTING.md). The bound is 50 ticks,
# 0.5 s: twice the worst figure for a loaded host, yet a copy that moved the
# whole 1 MiB in one step, 64 times the work, breaks it on every board.
COPY_STEP_BOUND_TICKS = 50
HOST_ALLOWANCE = "COPY_STEP_BOUND_TICKS"


def pattern(n):
    """The bytes of `fs pattern`: byte i is (i * 31 + 7) mod 256."""
    return bytes((i * 31 + 7) & 0xFF for i in range(n))


def first_line(data):
    line = data.split(b"\n")[0].rstrip(b"\r")[:60].decode()
    return line.replace(" ", "_") or "-"


def ok(final, **want):
    assert final["rc"] == "0", final
    for k, v in want.items():
        assert final[k] == str(v), (k, final)


def err(final, word):
    assert final["err"] == word and int(final["rc"]) < 0, final


def entries(items):
    return [m["entry"] for m in items if "entry" in m]


def home_card(tmp_path, extra=None, name="sd.img"):
    files = card_tree({
        "/UOS/HOME/ADMIN/SUB/INNER.TXT": b"inner file\n",
        "/UOS/HOME/ADMIN/EMPTY/": b"",
        "/UOS/HOME/ADMIN/AB.TXT": b"ab\n",
        "/UOS/HOME/ADMIN/AC.DAT": b"ac\n",
        "/UOS/HOME/ADMIN/ABC.TXT": b"abc\n",
        "/UOS/HOME/ADMIN/A.TXT": b"a\n",
        "/ROBCO/README.TXT": b"boot tree\n",
    })
    files.update(extra or {})
    return card(tmp_path, files, name=name)


def test_read_forms(board, uos_image, need_mtools, tmp_path):
    duty = card_tree()["/UOS/HOME/ADMIN/DUTY.TXT"]
    with alone(uos_image, board, home_card(tmp_path)) as vm:
        wait_ready(vm)
        assert answer(vm, "fs who ADMIN 0") == [
            "who home=ADMIN privs=0 default=SD0:[HOME.ADMIN]"]
        for spec in ("DUTY.TXT", "duty.txt;1", "SYS$LOGIN:DUTY.TXT", "SD0:[HOME.ADMIN]DUTY.TXT",
                     "DUTY", "[-.ADMIN]DUTY.TXT"):
            final, _ = fs(vm, f"cat {spec}")
            ok(final, len=len(duty), line=first_line(duty))
        ok(fs(vm, "cat [.SUB]INNER.TXT")[0], len=11, line="inner_file")
        err(fs(vm, f"cat DUTY.TXT {len(duty) - 1}")[0], "toobig")
        ok(fs(vm, f"cat DUTY.TXT {len(duty)}")[0], len=len(duty))
        err(fs(vm, "cat NOPE.TXT")[0], "notfound")
        err(fs(vm, "cat [.NOSUCH]X.TXT")[0], "dirnotfound")
        err(fs(vm, "cat SUB.DIR")[0], "isdir")
        err(fs(vm, "cat SUB.")[0], "isdir")
        err(fs(vm, "cat A<B.TXT")[0], "badspec")
        err(fs(vm, "cat SYS$LOGIN:[-]X.TXT")[0], "badspec")
        assert answer(vm, "fs default [.SUB]") == ["default SD0:[HOME.ADMIN.SUB]"]
        ok(fs(vm, "cat INNER.TXT")[0], len=11)
        reply = vm.script("fs default [.NOPE.]", timeout=TIMEOUT)
        assert reply.status == "err bad default", reply


def test_list(board, uos_image, need_mtools, tmp_path):
    many = {f"/UOS/HOME/ADMIN/MANY/F{i:02d}.TXT": b"x" for i in range(40)}
    with alone(uos_image, board, home_card(tmp_path, many)) as vm:
        wait_ready(vm)
        answer(vm, "fs who ADMIN 0")
        final, items = fs(vm, "ls *.*")
        names = sorted(n.split("/")[-1] for n in card_tree() if n.startswith("/UOS/HOME/ADMIN/"))
        want = sorted([f"{n};1" for n in names + ["AB.TXT", "AC.DAT", "ABC.TXT", "A.TXT"]] +
                      ["SUB.DIR;1", "EMPTY.DIR;1", "MANY.DIR;1"],
                      key=lambda s: (s.split(".")[0], s.split(".")[1]))
        assert entries(items) == want, items
        ok(final, count=len(want), total=len(want), more=0)
        for m in items:
            assert DATE.match(m["date"]) and TIME.match(m["time"]), m
            assert m["dir"] == ("1" if m["entry"].endswith(".DIR;1") else "0"), m
        assert [m["size"] for m in items if m["entry"] == "AB.TXT;1"] == ["3"]
        final, items = fs(vm, "ls *.TXT")
        assert all(e.endswith(".TXT;1") for e in entries(items)) and "AB.TXT;1" in entries(items)
        final, items = fs(vm, "ls A%.*")
        assert entries(items) == ["AB.TXT;1", "AC.DAT;1"], items
        ok(fs(vm, "ls [.EMPTY]")[0], count=0, total=0, more=0)
        final, items = fs(vm, "ls [.MANY]*.* 32")
        ok(final, count=32, total=40, more=1)
        assert entries(items) == [f"F{i:02d}.TXT;1" for i in range(32)]
        err(fs(vm, "ls [.NOSUCH]")[0], "dirnotfound")
        err(fs(vm, "ls [.A*]")[0], "badspec")


def test_write_copy_rename_delete(board, hooks_image, need_mtools, tmp_path):
    big = pattern(1 << 20)
    sd = home_card(tmp_path, {"/UOS/HOME/ADMIN/BIG.BIN": big})
    with alone(hooks_image, board, sd) as vm:
        wait_ready(vm)
        answer(vm, "fs who ADMIN 0")
        ok(fs(vm, "write NOTE.TXT hello there")[0])
        ok(fs(vm, "cat NOTE.TXT")[0], len=12, line="hello_there")
        ok(fs(vm, "write NOTE.TXT second")[0])
        ok(fs(vm, "cat NOTE.TXT")[0], len=7, line="second")
        err(fs(vm, "new NOTE.TXT third")[0], "exists")
        ok(fs(vm, "new FRESH.TXT third")[0])
        err(fs(vm, "write [.NOSUCH]X.TXT a")[0], "dirnotfound")
        err(fs(vm, "write SUB.DIR a")[0], "badspec")
        # Folders.
        ok(fs(vm, "mkdir [.NEW]")[0])
        err(fs(vm, "mkdir NEW.DIR")[0], "exists")
        err(fs(vm, "mkdir [.NOSUCH.DEEP]")[0], "dirnotfound")
        _, items = fs(vm, "ls NEW.*")
        assert entries(items) == ["NEW.DIR;1"], items
        ok(fs(vm, "stat [.NEW]")[0], exists=1, dir=1, name="NEW.DIR;1")
        ok(fs(vm, "stat NOTE.TXT")[0], exists=1, dir=0, size=7)
        err(fs(vm, "stat GONE.TXT")[0], "notfound")
        # Rename, also with a wildcard source and into a folder.
        ok(fs(vm, "ren NOTE.TXT NOTE.BAK")[0], files=1)
        err(fs(vm, "cat NOTE.TXT")[0], "notfound")
        err(fs(vm, "ren FRESH.TXT NOTE.BAK")[0], "exists")
        ok(fs(vm, "ren A*.TXT *.OLD")[0], files=3)
        _, items = fs(vm, "ls *.OLD")
        assert entries(items) == ["A.OLD;1", "AB.OLD;1", "ABC.OLD;1"], items
        ok(fs(vm, "ren FRESH.TXT [.NEW]")[0], files=1)
        ok(fs(vm, "cat [.NEW]FRESH.TXT")[0], len=6)
        # Copies: a small file, then 1 MiB in bounded steps.
        ok(fs(vm, "copy DUTY.TXT COPY.TXT")[0], files=1)
        duty = card_tree()["/UOS/HOME/ADMIN/DUTY.TXT"]
        ok(fs(vm, "crc COPY.TXT")[0], len=len(duty), crc=f"0x{zlib.crc32(duty):08x}")
        final, _ = fs(vm, "copy BIG.BIN BIG2.BIN", timeout=120)
        ok(final, files=1)
        assert int(final["steps"]) == (1 << 20) // 16384, final
        assert int(final["maxticks"]) <= COPY_STEP_BOUND_TICKS, (HOST_ALLOWANCE, final)
        want = f"0x{zlib.crc32(big):08x}"
        ok(fs(vm, "crc BIG2.BIN", timeout=120)[0], len=1 << 20, crc=want)
        # Copy over an existing file replaces it.
        ok(fs(vm, "copy AC.DAT COPY.TXT")[0])
        ok(fs(vm, "cat COPY.TXT")[0], len=3, line="ac")
        # Delete with a wildcard.
        final, items = fs(vm, "del *.OLD")
        ok(final, files=3, skipped=0)
        assert [m["file"] for m in items] == ["SD0:[HOME.ADMIN]A.OLD;1",
                                             "SD0:[HOME.ADMIN]AB.OLD;1",
                                             "SD0:[HOME.ADMIN]ABC.OLD;1"], items
        err(fs(vm, "del *.OLD")[0], "nofiles")
        err(fs(vm, "del SUB.DIR")[0], "isdir")
        # A pattern file large enough to span many clusters, then a reboot.
        ok(fs(vm, "pattern PAT.BIN 70000")[0])
        vm.send("reboot\n", port=1)
        vm.wait_for(REBOOTED, timeout=TIMEOUT)
        answer(vm, "fs who ADMIN 0")
        ok(fs(vm, "crc PAT.BIN")[0], len=70000, crc=f"0x{zlib.crc32(pattern(70000)):08x}")
        ok(fs(vm, "cat NOTE.BAK")[0], len=7, line="second")
        ok(fs(vm, "crc BIG2.BIN", timeout=120)[0], crc=want)
    names = card_names(sd, "UOS/HOME/ADMIN")
    assert not [n for n in names if n[0] in "~#"], names
    assert "NOTE.BAK" in names and "PAT.BIN" in names and "NEW" in names, names


def test_access(board, uos_image, need_mtools, tmp_path):
    with alone(uos_image, board, home_card(tmp_path)) as vm:
        wait_ready(vm)
        answer(vm, "fs who GUEST 0")
        err(fs(vm, "cat SD0:[HOME.ADMIN]DUTY.TXT")[0], "nopriv")
        err(fs(vm, "ls SD0:[HOME.ADMIN]")[0], "nopriv")
        err(fs(vm, "ls SD0:[000000]")[0], "nopriv")
        err(fs(vm, "write SD0:[HOME.ADMIN]X.TXT a")[0], "nopriv")
        err(fs(vm, "cat BOOT:README.TXT")[0], "nopriv")
        ok(fs(vm, "write MINE.TXT guest note")[0])
        answer(vm, f"fs who GUEST {READALL}")
        ok(fs(vm, "cat SD0:[HOME.ADMIN]DUTY.TXT")[0])
        ok(fs(vm, "ls SD0:[HOME.ADMIN]")[0])
        err(fs(vm, "del SD0:[HOME.ADMIN]DUTY.TXT")[0], "nopriv")
        err(fs(vm, "cat SD0:[000000]SYSUAF.DAT")[0], "nopriv")
        err(fs(vm, "cat SD0:[000000]SYSUAF.DEF")[0], "nopriv")
        answer(vm, f"fs who - {SECURITY}")
        ok(fs(vm, "cat SD0:[000000]SYSUAF.DAT 4096")[0])
        ok(fs(vm, "cat SD0:[000000]SYSUAF.DEF 4096")[0])
        err(fs(vm, "write SD0:[000000]SYSUAF.DEF x")[0], "protected")
        err(fs(vm, "write SD0:[000000]SYSUAF.DAT x")[0], "protected")
        answer(vm, f"fs who - {SYS}")
        err(fs(vm, "write SD0:[000000]SYSUAF.DAT x")[0], "protected")
        err(fs(vm, "del SD0:[000000]SYSUAF.DAT")[0], "protected")
        err(fs(vm, "ren SD0:[000000]UOS.CFG SYSUAF.DAT")[0], "protected")
        final, _ = fs(vm, "ls SD0:[000000]SYSUAF.DAT")
        ok(final, count=1)
        ok(fs(vm, "cat SD0:[000000]SYSUAF.DEF 4096")[0])
        err(fs(vm, "write SD0:[000000]SYSUAF.DEF x")[0], "protected")
        err(fs(vm, "del SD0:[000000]SYSUAF.DEF")[0], "protected")
        err(fs(vm, "copy SD0:[000000]UOS.CFG SD0:[000000]SYSUAF.DEF")[0], "protected")
        err(fs(vm, "ren SD0:[000000]UOS.CFG SYSUAF.DEF")[0], "protected")
        err(fs(vm, "ren SD0:[000000]SYSUAF.DEF SYSUAF.OLD")[0], "protected")
        ok(fs(vm, "cat BOOT:README.TXT")[0], len=10, line="boot_tree")
        err(fs(vm, "write BOOT:NEW.TXT x")[0], "readonly")
        err(fs(vm, "del BOOT:README.TXT")[0], "readonly")
        # A wildcard delete at the root checks every file: the card holds
        # SYSUAF.DAT and SYSUAF.DEF, and both stay.
        answer(vm, "fs default SD0:[000000]")
        final, items = fs(vm, "del SYSUAF.*")
        err(final, "nofiles")
        assert final["skipped"] == "2", final
        ok(fs(vm, "stat SYSUAF.DAT")[0], exists=1)
        ok(fs(vm, "stat SYSUAF.DEF")[0], exists=1)


def test_disk_full(board, hooks_image, need_mtools, tmp_path):
    keep = b"old content stays\n" * 200
    sd = tmp_path / "small.img"
    files = {k.lstrip("/"): v for k, v in card_tree({"/UOS/HOME/ADMIN/KEEP.TXT": keep}).items()}
    make_sd_image(sd, 4, files)
    card_put(sd, "UOS/FILL.BIN", bytes(card_free(sd) - 1024), tmp_path)
    assert card_free(sd) <= 2048
    with alone(hooks_image, board, sd) as vm:
        wait_ready(vm)
        answer(vm, "fs who ADMIN 0")
        err(fs(vm, "pattern KEEP.TXT 16384")[0], "full")
        ok(fs(vm, "crc KEEP.TXT")[0], len=len(keep), crc=f"0x{zlib.crc32(keep):08x}")
        err(fs(vm, "copy KEEP.TXT K2.TXT")[0], "full")
        final, _ = fs(vm, "free")
        ok(final)
        assert int(final["free"]) <= 2048, final
    names = card_names(sd, "UOS/HOME/ADMIN")
    assert not [n for n in names if n[0] in "~#"], names
    assert "K2.TXT" not in names



def test_copy_keeps_its_context(board, uos_image, need_mtools, tmp_path):
    """`fs default` and `fs who` sent between two files of a copy are
    refused with "busy", and every file lands in the target the copy
    resolved at its start. OTHER has its own DST: a copy that read the new
    default would land there with no error."""
    data = [pattern(1 << 19)[i:] + pattern(i) for i in (1, 2, 3)]
    extra = {f"/UOS/HOME/ADMIN/SRC/B{i + 1}.BIN": d for i, d in enumerate(data)}
    extra.update({"/UOS/HOME/ADMIN/DST/": b"", "/UOS/HOME/ADMIN/OTHER/DST/": b""})
    sd = home_card(tmp_path, extra)
    with alone(uos_image, board, sd) as vm:
        wait_ready(vm)
        answer(vm, "fs who ADMIN 0")
        start = len(vm.serial)
        answer(vm, "fs copy [.SRC]*.BIN [.DST]")
        vm.wait_for(re.compile(r"ev=fs op=copy file=SD0:\[HOME\.ADMIN\.SRC\]B2\.BIN"),
                    timeout=TIMEOUT)
        for command in ("fs default [.OTHER]", "fs who GUEST 0"):
            reply = vm.script(command, timeout=TIMEOUT)
            assert reply.status == "err busy", (command, reply)
        vm.wait_for(re.compile(r"ev=fs op=copy rc=[^\n]*\n"), timeout=120)
        final = [m for m in parse_markers(vm.serial[start:])
                 if m.get("ev") == "fs" and m.get("op") == "copy" and "rc" in m]
        ok(final[0], files=3, skipped=0)
        assert answer(vm, "fs default [.OTHER]") == ["default SD0:[HOME.ADMIN.OTHER]"]
        answer(vm, "fs who ADMIN 0")
        for i, d in enumerate(data):
            ok(fs(vm, f"crc [.DST]B{i + 1}.BIN", timeout=120)[0], len=len(d),
               crc=f"0x{zlib.crc32(d):08x}")
    assert sorted(card_names(sd, "UOS/HOME/ADMIN/DST")) == ["B1.BIN", "B2.BIN", "B3.BIN"]
    assert card_names(sd, "UOS/HOME/ADMIN/OTHER/DST") == []


def fat_data_bytes(sd):
    """Bytes of the data clusters of the card's FAT volume, from its boot
    sector: what the library's fs_statvfs() calls the total."""
    with open(sd, "rb") as f:
        f.seek(2048 * 512)
        bpb = f.read(512)
    sector, csize, reserved, nfats, roots, tot16, _, fat16 = struct.unpack_from(
        "<HBHBHHBH", bpb, 11)
    tot32, = struct.unpack_from("<I", bpb, 32)
    fatsz = fat16 or struct.unpack_from("<I", bpb, 36)[0]
    total = tot16 or tot32
    root_secs = (roots * 32 + sector - 1) // sector
    clusters = (total - reserved - nfats * fatsz - root_secs) // csize
    return clusters * csize * sector


def test_free_matches_the_card(board, uos_image, need_mtools, tmp_path):
    """`fs free` gives the card's real size and free bytes. A card over
    4 GiB is too large for a test image: the host test covers it."""
    sd = home_card(tmp_path)
    with alone(uos_image, board, sd) as vm:
        wait_ready(vm)
        final, _ = fs(vm, "free")
    ok(final, total=fat_data_bytes(sd), free=card_free(sd))

# Atomic writes cut between their renames: "#" holds the old content, "~"
# the new one. Recovery keeps "~" when both exist, "#" alone, and drops a
# lone "~" (a first write cut short).
CUT = {f"/UOS/HOME/ADMIN/{k}": v for k, v in {
    "#S1.TXT": b"old one\n",
    "~S2.TXT": b"cut short",
    "~S3.TXT": b"new three\n", "#S3.TXT": b"old three\n",
    "#T1.TXT": b"target old\n",
    "~T2.TXT": b"cut",
    "~T3.TXT": b"t3 new\n", "#T3.TXT": b"t3 old\n",
}.items()}


def test_interrupted_writes(board, uos_image, need_mtools, tmp_path):
    """Every state, as source and as target of a copy: the service sees the
    recovered files under their real names, never the work names."""
    sd = home_card(tmp_path, CUT)
    with alone(uos_image, board, sd) as vm:
        wait_ready(vm)
        answer(vm, "fs who ADMIN 0")
        # A listing changes nothing and shows what recovery will leave.
        _, items = fs(vm, "ls *.TXT")
        names = [e.split(";")[0] for e in entries(items)]
        assert [n for n in names if n[0] in "ST"] == ["S1.TXT", "S3.TXT", "T1.TXT", "T3.TXT"], \
            names
        assert [m["size"] for m in items if m["entry"] == "S3.TXT;1"] == ["10"], items
        # Copy sources: only "#", both, only "~".
        ok(fs(vm, "copy S1.TXT C1.TXT")[0], files=1)
        ok(fs(vm, "cat C1.TXT")[0], len=8, line="old_one")
        ok(fs(vm, "copy S3.TXT C3.TXT")[0], files=1)
        ok(fs(vm, "cat C3.TXT")[0], len=10, line="new_three")
        err(fs(vm, "copy S2.TXT C2.TXT")[0], "notfound")
        # Copy targets in new-only mode: recovery restores T1 and T3 first.
        err(fs(vm, "copy A.TXT T1.TXT new")[0], "exists")
        ok(fs(vm, "cat T1.TXT")[0], line="target_old")
        err(fs(vm, "copy A.TXT T3.TXT new")[0], "exists")
        ok(fs(vm, "cat T3.TXT")[0], line="t3_new")
        ok(fs(vm, "copy A.TXT T2.TXT new")[0], files=1)
        ok(fs(vm, "cat T2.TXT")[0], line="a")
        ok(fs(vm, "stat S1.TXT")[0], exists=1, size=8)
    names = card_names(sd, "UOS/HOME/ADMIN")
    assert not [n for n in names if n[0] in "~#"], names
    assert {"S1.TXT", "S3.TXT", "T1.TXT", "T2.TXT", "T3.TXT"} <= set(names), names
    assert "S2.TXT" not in names, names


def test_interrupted_writes_other_calls(board, uos_image, need_mtools, tmp_path):
    """stat, write new, delete, rename and mkdir see the recovered state."""
    extra = {f"/UOS/HOME/ADMIN/{k}": v for k, v in {
        "#D1.TXT": b"d\n", "#R1.TXT": b"r\n", "#R2.TXT": b"r2\n", "#W1.TXT": b"w\n",
        "#M1": b"file\n",
    }.items()}
    sd = home_card(tmp_path, extra)
    with alone(uos_image, board, sd) as vm:
        wait_ready(vm)
        answer(vm, "fs who ADMIN 0")
        ok(fs(vm, "stat R2.TXT")[0], exists=1, size=3)
        err(fs(vm, "new W1.TXT x")[0], "exists")
        ok(fs(vm, "cat W1.TXT")[0], line="w")
        ok(fs(vm, "del D1.TXT")[0], files=1)
        err(fs(vm, "ren R1.TXT R2.TXT")[0], "exists")
        ok(fs(vm, "ren R1.TXT R3.TXT")[0], files=1)
        ok(fs(vm, "cat R3.TXT")[0], line="r")
        err(fs(vm, "mkdir [.M1]")[0], "exists")
    names = card_names(sd, "UOS/HOME/ADMIN")
    assert not [n for n in names if n[0] in "~#"], names
    assert "D1.TXT" not in names and "R3.TXT" in names and "M1" in names, names


def no_card_checks(vm, word):
    for cmd in ("ls *.*", "cat DUTY.TXT", "write X.TXT a", "del X.TXT", "ren A.TXT B.TXT",
                "copy A.TXT B.TXT", "mkdir [.X]", "crc X.TXT", "stat X.TXT", "free"):
        err(fs(vm, cmd)[0], word)


def test_no_card(board, uos_image):
    with alone(uos_image, board) as vm:
        wait_ready(vm)
        no_card_checks(vm, "nocard")


def test_no_uos_tree(board, uos_image, need_mtools, tmp_path):
    with alone(uos_image, board, card(tmp_path, {"/ROBCO/README.TXT": b"x\n"})) as vm:
        wait_ready(vm)
        no_card_checks(vm, "notree")


def test_dev_hook(board, hooks_image, need_mtools, tmp_path):
    with alone(hooks_image, board, uos_card(tmp_path)) as vm:
        wait_ready(vm)
        answer(vm, "fs who GUEST 0")
        err(fs(vm, "cat DEV:DOOR1")[0], "nodev")
        err(fs(vm, "write DEV:DOOR1 OPEN")[0], "nodev")
        err(fs(vm, "ls DEV:")[0], "nodev")
        assert answer(vm, "fs fakedev on") == ["fakedev on"]
        ok(fs(vm, "cat DEV:DOOR1")[0], line="DOOR1_FAKE_IDLE")
        ok(fs(vm, "write DEV:DOOR1 OPEN")[0])
        ok(fs(vm, "cat DEV:DOOR1")[0], line="DOOR1_FAKE_OPEN")
        err(fs(vm, "cat DEV:NOPE")[0], "nodev")
        err(fs(vm, "del DEV:DOOR1")[0], "nopriv")
        final, items = fs(vm, "ls DEV:")
        ok(final, count=2, total=2)
        assert entries(items) == ["DOOR1.;1", "LAMP2.;1"], items
        err(fs(vm, "ls DEV:L*")[0], "badspec")
        assert answer(vm, "fs fakedev off") == ["fakedev off"]
        err(fs(vm, "cat DEV:DOOR1")[0], "nodev")
