#!/usr/bin/env python3
"""Build and check SYSUAF.DAT, the UOS user database.

  mkuaf.py build USERS.INI -o SYSUAF.DAT [--seed TEXT] [--iterations N]
  mkuaf.py check SYSUAF.DAT
  mkuaf.py vectors -o uaf.json
  mkuaf.py vectors-h uaf.json -o uaf_vectors.h

USERS.INI has one section per user, named by the user name:

  [ADMIN]
  password = secret       ; 1 to 32 printable ASCII characters
  privs = SYSTEM,OPER     ; optional, comma list, default none
  home = ADMIN            ; optional, default the user name
  flags = 2               ; optional integer, or a list of DISABLED,NOHACK

Exit codes: 0 good, 1 bad input or bad file, 2 bad usage.
"""

import argparse
import configparser
import hashlib
import json
import os
import re
import struct
import sys
import zlib

MAGIC = b"UOSUAF1\0"
VERSION = 1
HEADER_SIZE = 32
RECORD_SIZE = 96
MAX_USERS = 32
ITER_MIN = 1
ITER_MAX = 100000
ITER_DEFAULT = 2000
PRIVS = ["SYSTEM", "OPER", "SECURITY", "READALL"]
FLAGS = ["DISABLED", "NOHACK"]
PRIV_BITS = {n: 1 << i for i, n in enumerate(PRIVS)}
FLAG_BITS = {n: 1 << i for i, n in enumerate(FLAGS)}
NAME_RE = re.compile(r"^[A-Z0-9$_]{1,16}$")
HOME_RE = re.compile(r"^[A-Z0-9$_-]{1,8}$")


class UafError(Exception):
    pass


def uaf_hash(salt, password, iterations):
    pw = password.encode("ascii")
    h = hashlib.sha256(salt + pw).digest()
    for _ in range(iterations - 1):
        h = hashlib.sha256(h + salt + pw).digest()
    return h


def seed_salt(seed, name):
    return hashlib.sha256(seed.encode("utf-8") + b"\0" + name.encode("ascii")).digest()[:16]


def check_password(pw):
    if not 1 <= len(pw) <= 32:
        raise UafError("password must be 1 to 32 characters")
    if any(not 0x20 <= ord(c) <= 0x7E for c in pw):
        raise UafError("password must be printable ASCII")


def parse_bits(text, names, what):
    text = text.strip()
    if re.fullmatch(r"\d+|0x[0-9a-fA-F]+", text):
        return int(text, 0)
    bits = 0
    if text == "":
        return 0
    for item in text.split(","):
        item = item.strip().upper()
        if item not in names:
            raise UafError(f"unknown {what} {item!r}")
        bits |= 1 << names.index(item)
    return bits


def parse_users(text, notes=None):
    """Parse USERS.INI. Each name or home folded to upper case adds a note."""
    if notes is None:
        notes = []
    cp = configparser.ConfigParser(interpolation=None, inline_comment_prefixes=(";", "#"))
    cp.optionxform = str.lower
    try:
        cp.read_string(text)
    except configparser.Error as e:
        raise UafError(f"bad INI: {e.message}") from None
    users = []
    seen = set()
    for section in cp.sections():
        name = section.strip().upper()
        if not NAME_RE.match(name):
            raise UafError(f"[{section}]: user name must be 1 to 16 of A-Z 0-9 $ _")
        if name in seen:
            raise UafError(f"[{section}]: user appears twice")
        seen.add(name)
        if section.strip() != name:
            notes.append(f"[{section}]: note: stored as {name}")
        sec = cp[section]
        unknown = set(sec.keys()) - {"password", "privs", "home", "flags"}
        if unknown:
            raise UafError(f"[{section}]: unknown key {sorted(unknown)[0]!r}")
        if "password" not in sec:
            raise UafError(f"[{section}]: no password")
        try:
            check_password(sec["password"])
            privs = parse_bits(sec.get("privs", ""), PRIVS, "privilege")
            flags = parse_bits(sec.get("flags", ""), FLAGS, "flag")
        except UafError as e:
            raise UafError(f"[{section}]: {e}") from None
        if privs > 0xFFFFFFFF or flags > 0xFFFFFFFF:
            raise UafError(f"[{section}]: privs and flags must fit 32 bits")
        home = sec.get("home", name).strip().upper()
        if "home" in sec and sec["home"].strip() != home:
            notes.append(f"[{section}]: note: stored as {home}")
        if not HOME_RE.match(home):
            raise UafError(f"[{section}]: home must be 1 to 8 of A-Z 0-9 $ _ -")
        users.append({"name": name, "password": sec["password"], "privs": privs,
                      "flags": flags, "home": home})
    if len(users) > MAX_USERS:
        raise UafError(f"{len(users)} users, at most {MAX_USERS}")
    return users


def build(users, iterations, seed=None):
    if not ITER_MIN <= iterations <= ITER_MAX:
        raise UafError("iterations must be 1 to 100000")
    recs = b""
    for u in users:
        salt = seed_salt(seed, u["name"]) if seed is not None else os.urandom(16)
        h = uaf_hash(salt, u["password"], iterations)
        recs += struct.pack("<16s16s32sII16s8s", u["name"].encode("ascii"), salt, h,
                            u["privs"], u["flags"], u["home"].encode("ascii"), b"")
    header = MAGIC + struct.pack("<HHHHII8s", VERSION, RECORD_SIZE, len(users), 0,
                                 iterations, zlib.crc32(recs) & 0xFFFFFFFF, b"")
    return header + recs


def _field(raw, maxlen, regex, what, i):
    n = raw.find(b"\0")
    text = raw if n < 0 else raw[:n]
    if not text:
        raise UafError(f"record {i}: bad {what}")
    if any(raw[len(text):]):
        raise UafError(f"record {i}: {what} padding is not zero")
    try:
        s = text.decode("ascii")
    except UnicodeDecodeError:
        raise UafError(f"record {i}: bad {what}") from None
    if len(s) > maxlen or not regex.match(s):
        raise UafError(f"record {i}: bad {what}")
    return s


def parse_image(data):
    """Validates an image the same way uaf_validate does; returns (iterations, users)."""
    if len(data) < HEADER_SIZE:
        raise UafError("file shorter than its header")
    magic, ver, rsize, count, res, iters, crc, res2 = struct.unpack("<8sHHHHII8s", data[:32])
    if magic != MAGIC:
        raise UafError("bad magic")
    if ver != VERSION:
        raise UafError(f"version {ver}, expected 1")
    if rsize != RECORD_SIZE:
        raise UafError(f"record size {rsize}, expected 96")
    if count > MAX_USERS:
        raise UafError(f"count {count}, at most 32")
    if res != 0 or res2 != b"\0" * 8:
        raise UafError("header reserved bytes are not zero")
    if not ITER_MIN <= iters <= ITER_MAX:
        raise UafError(f"iterations {iters}, must be 1 to 100000")
    if len(data) != HEADER_SIZE + count * RECORD_SIZE:
        raise UafError(f"size {len(data)} does not match count {count}")
    if zlib.crc32(data[HEADER_SIZE:]) & 0xFFFFFFFF != crc:
        raise UafError("CRC mismatch")
    users = []
    names = set()
    for i in range(count):
        r = data[HEADER_SIZE + i * RECORD_SIZE:HEADER_SIZE + (i + 1) * RECORD_SIZE]
        name_b, salt, h, privs, flags, home_b, res3 = struct.unpack("<16s16s32sII16s8s", r)
        name = _field(name_b, 16, NAME_RE, "name", i)
        home = _field(home_b, 8, HOME_RE, "home", i)
        if res3 != b"\0" * 8:
            raise UafError(f"record {i}: reserved bytes are not zero")
        if name in names:
            raise UafError(f"record {i}: user {name} appears twice")
        names.add(name)
        users.append({"name": name, "privs": privs, "flags": flags, "home": home,
                      "salt": salt, "hash": h})
    return iters, users


def bits_text(bits, names):
    out = [n for i, n in enumerate(names) if bits & (1 << i)]
    extra = bits & ~((1 << len(names)) - 1)
    if extra:
        out.append(f"0x{extra:X}")
    return ",".join(out) if out else "-"


VECTOR_CASES = [
    ("ADMIN", "a", 1),
    ("OPER", "a", 2000),
    ("GUEST", "password", 1),
    ("USER1", "Pass word 7!", 2),
    ("SYS$MGR", "1234567", 10),       # 48 + 7 = 55 bytes: one block per round
    ("X", "12345678", 10),            # 48 + 8 = 56 bytes: two blocks per round
    ("LONGNAME_16CHARS", "~" * 32, 1),
    ("MAX", "!\"#$%&'()*+,-./0123456789:;<=>?", 2000),
    ("SPACE", " ", 3),
    ("MIXED", "AbCdEfGhIjKlMnOpQrStUvWxYz012345", 100),
]


def make_vectors():
    out = []
    for name, pw, it in VECTOR_CASES:
        salt = seed_salt("uaf-vectors", name)
        out.append({"name": name, "password": pw, "salt": salt.hex(), "iterations": it,
                    "hash": uaf_hash(salt, pw, it).hex()})
    return out


def c_string(s):
    return '"' + "".join("\\" + c if c in '"\\' else c for c in s) + '"'


def vectors_header(vectors):
    lines = ["/* Generated by tools/mkuaf.py vectors-h from tests/vectors/uaf.json. */",
             "static const struct uaf_vector {",
             "    const char *name;", "    const char *password;", "    const char *salt;",
             "    unsigned iterations;", "    const char *hash;",
             "} uaf_vectors[] = {"]
    for v in vectors:
        lines.append(f"    {{ {c_string(v['name'])}, {c_string(v['password'])}, "
                     f"\"{v['salt']}\", {int(v['iterations'])}u, \"{v['hash']}\" }},")
    lines.append("};")
    return "\n".join(lines) + "\n"


def fail(msg):
    print(f"mkuaf: {msg}", file=sys.stderr)
    return 1


def main(argv=None):
    ap = argparse.ArgumentParser(prog="mkuaf.py", description="Build and check SYSUAF.DAT.")
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build", help="build SYSUAF.DAT from USERS.INI")
    b.add_argument("ini")
    b.add_argument("-o", "--output", required=True)
    b.add_argument("--seed", help="derive salts from this text (reproducible output)")
    b.add_argument("--iterations", type=int, default=ITER_DEFAULT)
    c = sub.add_parser("check", help="validate a file and list its users")
    c.add_argument("file")
    v = sub.add_parser("vectors", help="write the shared test vectors as JSON")
    v.add_argument("-o", "--output", required=True)
    vh = sub.add_parser("vectors-h", help="turn the JSON vectors into a C header")
    vh.add_argument("json")
    vh.add_argument("-o", "--output", required=True)
    args = ap.parse_args(argv)

    try:
        if args.cmd == "build":
            try:
                with open(args.ini, encoding="ascii") as f:
                    text = f.read()
            except OSError as e:
                return fail(f"cannot read {args.ini}: {e.strerror}")
            except UnicodeDecodeError:
                return fail(f"{args.ini}: not ASCII")
            notes = []
            users = parse_users(text, notes)
            admin = PRIV_BITS["SYSTEM"] | PRIV_BITS["SECURITY"]
            if not any(u["privs"] & admin and not u["flags"] & FLAG_BITS["DISABLED"]
                       for u in users):
                notes.append("note: no enabled user holds SYSTEM or SECURITY, "
                             "nobody can run AUTHORIZE")
            for note in notes:
                print(f"mkuaf: {args.ini}: {note}", file=sys.stderr)
            data = build(users, args.iterations, args.seed)
            with open(args.output, "wb") as f:
                f.write(data)
            return 0
        if args.cmd == "check":
            try:
                with open(args.file, "rb") as f:
                    data = f.read()
            except OSError as e:
                return fail(f"cannot read {args.file}: {e.strerror}")
            iters, users = parse_image(data)
            print(f"{args.file}: {len(users)} users, {iters} iterations")
            for u in users:
                print(f"{u['name']:<16} home={u['home']:<8} privs={bits_text(u['privs'], PRIVS)} "
                      f"flags={bits_text(u['flags'], FLAGS)}")
            return 0
        if args.cmd == "vectors":
            with open(args.output, "w", encoding="ascii") as f:
                json.dump(make_vectors(), f, indent=2)
                f.write("\n")
            return 0
        if args.cmd == "vectors-h":
            try:
                with open(args.json, encoding="ascii") as f:
                    vectors = json.load(f)
            except (OSError, ValueError) as e:
                return fail(f"cannot read {args.json}: {e}")
            with open(args.output, "w", encoding="ascii") as f:
                f.write(vectors_header(vectors))
            return 0
    except UafError as e:
        name = getattr(args, "ini", None) or getattr(args, "file", "")
        return fail(f"{name}: {e}")
    except OSError as e:
        return fail(f"cannot write {e.filename}: {e.strerror}")
    return 2


if __name__ == "__main__":
    sys.exit(main())
