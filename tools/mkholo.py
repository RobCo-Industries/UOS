#!/usr/bin/env python3
"""Build, list and check UOS data holotapes. See docs/modules/holo.md.

    mkholo.py build --title TEXT --out NAME.HTP [--rimg PATH] FOLDER
    mkholo.py list [--rimg PATH] NAME.HTP
    mkholo.py check [--rimg PATH] NAME.HTP...

build takes the .TXT files of FOLDER in name order. With ORDER.TXT, each
non-blank line not starting with '#' is "FILE.TXT Entry title" and gives
the order; every .TXT file must be listed. Without it, the first line of
each file is the entry title and the rest is the body.

The rimg layer comes from RobCo-Core's tools/rimg.py. Every command uses
--rimg if given, else $ROBCO_CORE/tools/rimg.py if ROBCO_CORE is set, else
external/robco-core/tools/rimg.py beside this tools/ folder. A path given
by --rimg or ROBCO_CORE that does not exist is an error, with no fallback.
rimg.py seal and pack refuse data
tapes because subtype 2 has no slot, so build reuses rimg.py's layout
constants, title encoding and CRC and writes the header itself.

Exit codes: 0 ok, 1 bad input or invalid tape, 2 a file cannot be read or
written, 3 rimg.py not found, 64 usage.
"""

import argparse
import importlib.util
import os
import struct
import sys

EX_OK, EX_BAD, EX_IO, EX_RIMG, EX_USAGE = 0, 1, 2, 3, 64

PAYLOAD_OFFSET = 0x120
SUBTYPE_DATA = 2
STUB_WORD = 0xEAFFFFFE          # ARM "b ." : a data tape never runs
ABI_VERSION = 1 << 16
ARC_MAGIC = b"HTXT"
ARC_VERSION = 1
ARC_HDR = 8
ENTRY_SIZE = 40
NAME_FIELD = 32
NAME_MAX = 31
TITLE_FIELD = 64
TITLE_MAX = 63
ENTRIES_MAX = 32
TEXT_MAX = 65536
TAPE_MAX = PAYLOAD_OFFSET + ARC_HDR + ENTRIES_MAX * ENTRY_SIZE + ENTRIES_MAX * TEXT_MAX

# Same names and order as hf_err_t in src/holo/holofmt.h.
ERRORS = ["ok", "arg", "toobig", "io", "magic", "version", "size", "subtype", "bounds",
          "abi", "crc", "title", "arcshort", "arcmagic", "arcver", "count", "table",
          "name", "length", "range", "overlap"]


class BadInput(Exception):
    pass


class TapeError(Exception):
    def __init__(self, name, msg):
        super().__init__(msg)
        self.name = name


RIMG_NOT_FOUND = "rimg.py not found: give --rimg, set ROBCO_CORE, or run from a UOS checkout"


class RimgMissing(Exception):
    pass


def rimg_path(path=None):
    """The rimg.py to load. A path the user gave is never replaced by another."""
    if path:
        if not os.path.isfile(path):
            raise RimgMissing(f"--rimg {path}: no such file")
        return path
    core = os.environ.get("ROBCO_CORE")
    if core:
        found = os.path.join(core, "tools", "rimg.py")
        if not os.path.isfile(found):
            raise RimgMissing(f"ROBCO_CORE is set and {found} does not exist")
        return found
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    found = os.path.join(here, "external", "robco-core", "tools", "rimg.py")
    if not os.path.isfile(found):
        raise RimgMissing(RIMG_NOT_FOUND)
    return found


def load_rimg(path=None):
    path = rimg_path(path)
    spec = importlib.util.spec_from_file_location("rimg", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def printable(raw):
    return all(0x20 <= c <= 0x7E for c in raw)


def text_ok(raw):
    return all(0x20 <= c <= 0x7E or c in (9, 10, 13) for c in raw)


def check_body(where, body, size=True):
    for i, c in enumerate(body):
        if not (0x20 <= c <= 0x7E or c in (9, 10, 13)):
            raise BadInput(f"{where}: byte {c:#04x} at offset {i} is not printable ASCII,"
                           " tab, CR or LF")
    if size and len(body) > TEXT_MAX:
        raise BadInput(f"{where}: entry is {len(body)} bytes, more than {TEXT_MAX}")


def check_entry_title(where, title):
    raw = title.encode("ascii", "replace")
    if not title or not printable(raw) or any(ord(ch) > 0x7E for ch in title):
        raise BadInput(f"{where}: entry title is empty or not printable ASCII")
    if len(raw) > NAME_MAX:
        raise BadInput(f"{where}: entry title is {len(raw)} characters, more than {NAME_MAX}")
    return raw


def read_bytes(path):
    with open(path, "rb") as f:
        return f.read()


def collect(folder):
    """Returns [(title bytes, body bytes)] from a folder."""
    names = sorted(n for n in os.listdir(folder)
                   if n.upper().endswith(".TXT") and n.upper() != "ORDER.TXT"
                   and os.path.isfile(os.path.join(folder, n)))
    order = next((n for n in os.listdir(folder) if n.upper() == "ORDER.TXT"), None)
    entries = []
    if order is not None:
        raw = read_bytes(os.path.join(folder, order))
        if not text_ok(raw):
            raise BadInput(f"{order}: not plain ASCII")
        listed = []
        for num, line in enumerate(raw.decode("ascii").splitlines(), 1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split(None, 1)
            if len(parts) != 2:
                raise BadInput(f"{order} line {num}: expected FILE.TXT and an entry title")
            fname, title = parts
            match = next((n for n in names if n.upper() == fname.upper()), None)
            if match is None:
                raise BadInput(f"{order} line {num}: {fname} is not a .TXT file in the folder")
            if match in listed:
                raise BadInput(f"{order} line {num}: {fname} is listed twice")
            listed.append(match)
            body = read_bytes(os.path.join(folder, match))
            check_body(match, body)
            entries.append((check_entry_title(f"{order} line {num}", title), body))
        missing = [n for n in names if n not in listed]
        if missing:
            raise BadInput(f"{order}: {missing[0]} is not listed")
    else:
        for n in names:
            data = read_bytes(os.path.join(folder, n))
            check_body(n, data, size=False)
            nl = data.find(b"\n")
            first, body = (data, b"") if nl < 0 else (data[:nl], data[nl + 1:])
            check_body(n, body)
            title = first.rstrip(b"\r").strip().decode("ascii")
            entries.append((check_entry_title(n, title), body))
    if not entries:
        raise BadInput(f"{folder}: no .TXT files")
    if len(entries) > ENTRIES_MAX:
        raise BadInput(f"{folder}: {len(entries)} entries, more than {ENTRIES_MAX}")
    return entries


def archive(entries):
    table_end = ARC_HDR + len(entries) * ENTRY_SIZE
    out = bytearray(ARC_MAGIC + struct.pack("<HH", ARC_VERSION, len(entries)))
    bodies = bytearray()
    for title, body in entries:
        out += title.ljust(NAME_FIELD, b"\0")
        out += struct.pack("<II", table_end + len(bodies), len(body))
        bodies += body
    return bytes(out + bodies)


def tape_title(rimg, title):
    try:
        raw = title.encode("ascii")
    except UnicodeEncodeError:
        raise BadInput("tape title is not ASCII")
    if not raw or not printable(raw):
        raise BadInput("tape title is empty or not printable ASCII")
    if len(raw) > TITLE_MAX:
        raise BadInput(f"tape title is {len(raw)} characters, more than {TITLE_MAX}")
    return rimg.title_bytes(title)


def short_name(out):
    stem = os.path.splitext(os.path.basename(out))[0].upper()
    raw = stem.encode("ascii", "replace")[:16]
    return raw


def seal(rimg, title, name, payload):
    """A sealed data tape: stub, header, META, payload."""
    size = PAYLOAD_OFFSET + len(payload)
    data = bytearray(size)
    struct.pack_into("<I", data, 0, STUB_WORD)
    struct.pack_into(rimg.HDR_FMT, data, rimg.HDR_OFFSET, rimg.MAGICS["ROBCO78"], 1,
                     rimg.HDR_SIZE, rimg.FLAG_META, 0, size, size, 0, ABI_VERSION, 0,
                     SUBTYPE_DATA, name, 0)
    data[rimg.META_OFFSET:rimg.META_OFFSET + rimg.TITLE_SIZE] = tape_title(rimg, title)
    data[PAYLOAD_OFFSET:] = payload
    struct.pack_into("<I", data, rimg.CRC_OFFSET, rimg.image_crc(data, size))
    return bytes(data)


def build_tape(rimg, title, folder, out):
    tape_title(rimg, title)
    entries = collect(folder)
    data = seal(rimg, title, short_name(out), archive(entries))
    validate(rimg, data)
    return data


def field_ok(raw, maxlen):
    nul = raw.find(b"\0")
    if nul < 0:
        return False
    return 1 <= nul <= maxlen and printable(raw[:nul]) and not any(raw[nul:])


def validate(rimg, data):
    """Mirrors hf_open() check for check. Returns (title, [(name, body)])."""
    size = len(data)
    if size > TAPE_MAX:
        raise TapeError("toobig", "tape file is too large")
    if size < rimg.HDR_END:
        raise TapeError("io", "tape file is too short")
    h = rimg.parse_header(data)
    if h["magic"] != rimg.MAGICS["ROBCO78"]:
        raise TapeError("magic", "not a holotape")
    if (h["hdr_version"] != 1 or h["hdr_size"] != rimg.HDR_SIZE
            or h["flags"] != rimg.FLAG_META or h["reserved"] != 0):
        raise TapeError("version", "unsupported tape header")
    if h["file_size"] != size or size < PAYLOAD_OFFSET:
        raise TapeError("size", "tape size does not match the file")
    if h["subtype"] != SUBTYPE_DATA:
        raise TapeError("subtype", "not a data tape")
    if h["load_addr"] != 0 or h["mem_size"] != size:
        raise TapeError("bounds", "bad load address or memory size")
    if h["abi_version"] >> 16 != 1:
        raise TapeError("abi", "unsupported tape version")
    if rimg.image_crc(data, size) != h["crc32"]:
        raise TapeError("crc", "tape is damaged")
    meta = data[rimg.META_OFFSET:rimg.META_OFFSET + TITLE_FIELD]
    if not field_ok(meta, TITLE_MAX):
        raise TapeError("title", "tape title is missing or not plain text")
    pl = data[PAYLOAD_OFFSET:]
    if len(pl) < ARC_HDR:
        raise TapeError("arcshort", "tape holds no archive")
    if pl[:4] != ARC_MAGIC:
        raise TapeError("arcmagic", "archive magic is not HTXT")
    version, count = struct.unpack_from("<HH", pl, 4)
    if version != ARC_VERSION:
        raise TapeError("arcver", "unsupported archive version")
    if not 1 <= count <= ENTRIES_MAX:
        raise TapeError("count", "entry count is not 1 to 32")
    table_end = ARC_HDR + count * ENTRY_SIZE
    if table_end > len(pl):
        raise TapeError("table", "entry table runs past the tape end")
    ents = []
    for i in range(count):
        e = pl[ARC_HDR + i * ENTRY_SIZE:ARC_HDR + (i + 1) * ENTRY_SIZE]
        off, ln = struct.unpack_from("<II", e, NAME_FIELD)
        if not field_ok(e[:NAME_FIELD], NAME_MAX):
            raise TapeError("name", "entry name is missing or not plain text")
        if ln > TEXT_MAX:
            raise TapeError("length", "entry is longer than 64 KiB")
        if off < table_end or off > len(pl) or ln > len(pl) - off:
            raise TapeError("range", "entry text lies outside the tape")
        ents.append((e[:NAME_FIELD].rstrip(b"\0").decode("ascii"), off, ln))
    for i in range(count):
        for j in range(i + 1, count):
            _, ao, al = ents[i]
            _, bo, bl = ents[j]
            if al and bl and ao < bo + bl and bo < ao + al:
                raise TapeError("overlap", "two entries share text")
    title = meta.rstrip(b"\0").decode("ascii")
    return title, [(n, pl[o:o + ln]) for n, o, ln in ents]


class Parser(argparse.ArgumentParser):
    def error(self, message):
        self.print_usage(sys.stderr)
        print(f"mkholo: usage error: {message}", file=sys.stderr)
        sys.exit(EX_USAGE)


def main(argv=None):
    p = Parser(prog="mkholo", description="Build, list and check UOS data holotapes.")
    sub = p.add_subparsers(dest="cmd", parser_class=Parser)
    sub.required = True
    b = sub.add_parser("build", help="build a tape from a folder of .TXT files")
    b.add_argument("--title", required=True)
    b.add_argument("--out", required=True)
    b.add_argument("--rimg", metavar="PATH")
    b.add_argument("folder")
    li = sub.add_parser("list", help="show a tape's title and entries")
    li.add_argument("--rimg", metavar="PATH")
    li.add_argument("tape")
    c = sub.add_parser("check", help="validate tapes")
    c.add_argument("--rimg", metavar="PATH")
    c.add_argument("tapes", nargs="+")
    args = p.parse_args(argv)

    try:
        rimg = load_rimg(args.rimg)
    except RimgMissing as e:
        print(f"mkholo: error: {e}", file=sys.stderr)
        return EX_RIMG

    try:
        if args.cmd == "build":
            if not os.path.isdir(args.folder):
                print(f"mkholo: error: {args.folder}: not a folder", file=sys.stderr)
                return EX_IO
            data = build_tape(rimg, args.title, args.folder, args.out)
            with open(args.out, "wb") as f:
                f.write(data)
            print(f"{args.out}: {len(data)} bytes")
        elif args.cmd == "list":
            title, ents = validate(rimg, read_bytes(args.tape))
            print(f"title: {title}")
            for i, (name, body) in enumerate(ents, 1):
                print(f"{i:02d}  {name}  ({len(body)} bytes)")
        else:
            rc = EX_OK
            for path in args.tapes:
                try:
                    validate(rimg, read_bytes(path))
                    print(f"{path}: ok")
                except TapeError as e:
                    print(f"{path}: {e.name}: {e}")
                    rc = EX_BAD
            return rc
    except BadInput as e:
        print(f"mkholo: error: {e}", file=sys.stderr)
        return EX_BAD
    except TapeError as e:
        print(f"mkholo: error: {args.tape}: {e.name}: {e}", file=sys.stderr)
        return EX_BAD
    except OSError as e:
        print(f"mkholo: error: {e.filename}: {e.strerror}", file=sys.stderr)
        return EX_IO
    return EX_OK


if __name__ == "__main__":
    sys.exit(main())
