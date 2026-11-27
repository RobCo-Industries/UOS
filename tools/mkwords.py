#!/usr/bin/env python3
"""Check, sort and sample the minigame word lists W04.TXT to W12.TXT.

  mkwords.py check [--strict] [DIR] validate every list, exit 1 on a problem
  mkwords.py sort [DIR]             rewrite every list sorted, LF line ends
  mkwords.py sample N [--length L] [--seed S] [DIR]
                                    print N distinct random words of one list

check fails only on what UOS refuses: a character outside A to Z, a wrong
length, a repeated word, more than 512 words, fewer words than the board
needs, or a file larger than FILE_MAX bytes. A missing list, an unsorted list, fewer than 64 words and a
deny-list word print as "note:" and keep exit 0. --strict makes every note
a failure. The last line counts the good lists present.

DIR defaults to card/UOS/WORDS. sample uses Python's random module and does
not mirror the C selection rule.
"""
import argparse
import os
import random
import sys

LENGTHS = range(4, 13)
MIN_WORDS = 64
MAX_WORDS = 512  # HK_LIST_MAX in src/hack/hack.h
# Words on the board, per word length: the hk_diff table in src/hack/hack.c.
BOARD_WORDS = {4: 8, 5: 8, 6: 12, 7: 12, 8: 12, 9: 14, 10: 14, 11: 16, 12: 16}
# WORDS_FILE_MAX in src/hack/hackscr.c: UOS refuses a list file larger
# than this many bytes on the card, line ends included.
FILE_MAX = MAX_WORDS * (12 + 2) + 64
DEFAULT_DIR = os.path.join("card", "UOS", "WORDS")

# Words a list must never hold: violence, harm and insults.
DENY = frozenset("""
BLOOD BOMB CORPSE CRUEL DEAD DEATH DIE GUN HATE IDIOT KILL KILLER MURDER
POISON RIFLE SHOOT SLAVE STUPID SUICIDE TORTURE UGLY WAR WEAPON
""".split())


def list_name(n):
    return "W%02d.TXT" % n


def read_words(path):
    with open(path, "rb") as f:
        text = f.read().decode("ascii", errors="replace")
    return [line.rstrip("\r") for line in text.split("\n") if line.rstrip("\r") != ""]


def check_list(path, n):
    """Returns (errors, notes) for one file. Errors are what UOS refuses."""
    if not os.path.isfile(path):
        return [], ["%s: missing" % path]
    errors, notes = [], []
    size = os.path.getsize(path)
    if size > FILE_MAX:
        errors.append("%s: %d bytes, UOS reads at most %d" % (path, size, FILE_MAX))
    words = read_words(path)
    seen = set()
    for i, w in enumerate(words, 1):
        if not (w.isascii() and w.isalpha() and w.isupper()):
            errors.append("%s: word %d %r: not A to Z" % (path, i, w))
        elif len(w) != n:
            errors.append("%s: word %d %r: length %d, want %d" % (path, i, w, len(w), n))
        if w in seen:
            errors.append("%s: word %d %r: duplicate" % (path, i, w))
        seen.add(w)
        if w in DENY:
            notes.append("%s: word %d %r: on the deny list" % (path, i, w))
    if words != sorted(words):
        notes.append("%s: not sorted" % path)
    if len(words) < BOARD_WORDS[n]:
        errors.append("%s: %d words, the board needs %d" % (path, len(words), BOARD_WORDS[n]))
    elif len(words) < MIN_WORDS:
        notes.append("%s: %d words, want at least %d" % (path, len(words), MIN_WORDS))
    if len(words) > MAX_WORDS:
        errors.append("%s: %d words, want at most %d" % (path, len(words), MAX_WORDS))
    return errors, notes


def cmd_check(d, strict=False):
    good = 0
    failed = False
    for n in LENGTHS:
        path = os.path.join(d, list_name(n))
        errors, notes = check_list(path, n)
        for e in errors:
            print(e, file=sys.stderr)
        for note in notes:
            print(("%s" if strict else "note: %s") % note, file=sys.stderr)
        bad = errors or (strict and notes)
        failed = failed or bool(bad)
        if os.path.isfile(path) and not bad:
            good += 1
    print("mkwords: %d lists ok" % good)
    return 1 if failed else 0


def cmd_sort(d):
    for n in LENGTHS:
        path = os.path.join(d, list_name(n))
        if not os.path.isfile(path):
            continue
        words = sorted(set(read_words(path)))
        with open(path, "w", newline="") as f:
            f.write("".join(w + "\n" for w in words))
    return 0


def cmd_sample(d, count, length, seed):
    words = read_words(os.path.join(d, list_name(length)))
    if count < 1 or count > len(words):
        print("mkwords: sample size must be 1 to %d" % len(words), file=sys.stderr)
        return 1
    for w in random.Random(seed).sample(words, count):
        print(w)
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description="Check, sort and sample the word lists.")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("check")
    p.add_argument("--strict", action="store_true", help="make every note a failure")
    p.add_argument("dir", nargs="?", default=DEFAULT_DIR)
    p = sub.add_parser("sort")
    p.add_argument("dir", nargs="?", default=DEFAULT_DIR)
    p = sub.add_parser("sample")
    p.add_argument("count", type=int)
    p.add_argument("--length", type=int, default=6, choices=list(LENGTHS))
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("dir", nargs="?", default=DEFAULT_DIR)
    a = ap.parse_args(argv)
    if a.cmd == "check":
        return cmd_check(a.dir, a.strict)
    if a.cmd == "sort":
        return cmd_sort(a.dir)
    return cmd_sample(a.dir, a.count, a.length, a.seed)


if __name__ == "__main__":
    sys.exit(main())
