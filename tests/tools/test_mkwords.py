"""Tests for tools/mkwords.py. Run with pytest, or directly with python3."""
import contextlib
import io
import os
import re
import shutil
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import mkwords  # noqa: E402

CARD = os.path.join(ROOT, "card", "UOS", "WORDS")


def run(argv):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        rc = mkwords.main(argv)
    return rc, out.getvalue(), err.getvalue()


def make_dir():
    d = tempfile.mkdtemp()
    for n in mkwords.LENGTHS:
        shutil.copy(os.path.join(CARD, mkwords.list_name(n)), d)
    return d


def write(d, n, words, crlf=False):
    end = "\r\n" if crlf else "\n"
    with open(os.path.join(d, mkwords.list_name(n)), "w", newline="") as f:
        f.write("".join(w + end for w in words))


def test_card_lists_pass():
    rc, out, err = run(["check", CARD])
    assert rc == 0, err
    assert "9 lists ok" in out


def test_card_lists_sizes():
    for n in mkwords.LENGTHS:
        words = mkwords.read_words(os.path.join(CARD, mkwords.list_name(n)))
        assert len(words) >= 64
        if n <= 8:
            assert len(words) >= 100


def bad_case(n, words, expect, crlf=False):
    d = make_dir()
    try:
        write(d, n, words, crlf)
        rc, _, err = run(["check", d])
        assert rc == 1
        assert expect in err, err
    finally:
        shutil.rmtree(d)


def note_case(n, words, expect):
    """A note keeps exit 0 by default and fails under --strict."""
    d = make_dir()
    try:
        if words is None:
            os.remove(os.path.join(d, mkwords.list_name(n)))
        else:
            write(d, n, words)
        rc, out, err = run(["check", d])
        assert rc == 0, err
        assert "note: " in err and expect in err, err
        assert "8 lists ok" in out if words is None else "9 lists ok" in out
        rc, out, err = run(["check", "--strict", d])
        assert rc == 1 and expect in err and "note: " not in err, err
        assert "8 lists ok" in out
    finally:
        shutil.rmtree(d)


def base(n=4):
    return mkwords.read_words(os.path.join(CARD, mkwords.list_name(n)))


def test_wrong_length():
    bad_case(4, sorted(base() + ["ABCDE"]), "length 5, want 4")


def test_bad_alphabet():
    bad_case(4, sorted(base()[1:] + ["AB1D"]), "not A to Z")
    bad_case(4, sorted(base()[1:] + ["abcd"]), "not A to Z")


def test_duplicate():
    w = base()
    bad_case(4, sorted(w + [w[0]]), "duplicate")


def test_unsorted():
    note_case(4, list(reversed(base())), "not sorted")


def test_too_few():
    note_case(4, base()[:63], "63 words, want at least 64")


def test_fewer_than_the_board_needs():
    bad_case(4, base()[:7], "7 words, the board needs 8")
    bad_case(6, base(6)[:11], "11 words, the board needs 12")
    bad_case(9, base(9)[:13], "13 words, the board needs 14")
    bad_case(12, base(12)[:15], "15 words, the board needs 16")


def test_board_minimum_ok():
    d = make_dir()
    try:
        for n, need in mkwords.BOARD_WORDS.items():
            write(d, n, base(n)[:need])
        rc, out, err = run(["check", d])
        assert rc == 0, err
        assert "9 lists ok" in out
    finally:
        shutil.rmtree(d)


def test_too_many():
    words = ["Q%c%cZ" % (chr(65 + i // 26), chr(65 + i % 26)) for i in range(513)]
    bad_case(4, words, "513 words, want at most 512")


def test_file_max_matches_uos():
    defs = {}
    for name in ("hack/hack.h", "hack/hackscr.c"):
        with open(os.path.join(ROOT, "src", name)) as f:
            for m in re.finditer(r"^#define\s+(\w+)\s+(.+?)\s*(?:/\*.*)?$", f.read(), re.M):
                defs[m.group(1)] = m.group(2)
    expr = defs["WORDS_FILE_MAX"]
    for k in ("HK_LIST_MAX", "HK_WORD_MAX"):
        expr = expr.replace(k, defs[k])
    assert re.fullmatch(r"[0-9()+* ]+", expr), expr
    assert mkwords.FILE_MAX == eval(expr)


def sized_list(size):
    """Good sorted 12-letter CR LF words, padded with blank lines to size bytes."""
    words = base(12)
    body = "".join(w + "\r\n" for w in words)
    assert len(body) <= size
    return body + "\n" * (size - len(body))


def test_file_at_limit_ok():
    d = make_dir()
    try:
        with open(os.path.join(d, mkwords.list_name(12)), "w", newline="") as f:
            f.write(sized_list(mkwords.FILE_MAX))
        rc, out, err = run(["check", d])
        assert rc == 0, err
        assert "9 lists ok" in out
    finally:
        shutil.rmtree(d)


def test_file_over_limit_fails():
    d = make_dir()
    try:
        path = os.path.join(d, mkwords.list_name(12))
        with open(path, "w", newline="") as f:
            f.write(sized_list(mkwords.FILE_MAX + 1))
        rc, out, err = run(["check", d])
        assert rc == 1
        assert "%s: %d bytes, UOS reads at most %d" % (path, mkwords.FILE_MAX + 1,
                                                       mkwords.FILE_MAX) in err
    finally:
        shutil.rmtree(d)


def test_unsorted_short_list_passes_with_notes():
    d = make_dir()
    try:
        write(d, 6, list(reversed(base(6)[:20])))
        rc, out, err = run(["check", d])
        assert rc == 0, err
        assert "note: " in err and "not sorted" in err and "20 words" in err
        assert "9 lists ok" in out
        rc, _, err = run(["check", "--strict", d])
        assert rc == 1
    finally:
        shutil.rmtree(d)


def test_card_lists_pass_strict():
    rc, out, err = run(["check", "--strict", CARD])
    assert rc == 0, err
    assert "9 lists ok" in out and err == ""


def test_at_minimum_ok():
    d = make_dir()
    try:
        write(d, 4, base()[:64], crlf=True)
        rc, _, err = run(["check", d])
        assert rc == 0, err
    finally:
        shutil.rmtree(d)


def test_deny_list():
    note_case(4, sorted(base() + ["KILL"]), "deny list")


def test_missing_file():
    note_case(7, None, "missing")


def test_sort_fixes_order_and_dups():
    d = make_dir()
    try:
        w = base(5)
        write(d, 5, list(reversed(w)) + [w[3]], crlf=True)
        rc, _, _ = run(["sort", d])
        assert rc == 0
        assert mkwords.read_words(os.path.join(d, "W05.TXT")) == sorted(w)
        rc, _, err = run(["check", d])
        assert rc == 0, err
    finally:
        shutil.rmtree(d)


def test_sample():
    rc, out, _ = run(["sample", "8", "--length", "6", "--seed", "7", CARD])
    assert rc == 0
    words = out.split()
    assert len(words) == 8 and len(set(words)) == 8
    assert all(len(w) == 6 for w in words)
    rc2, out2, _ = run(["sample", "8", "--length", "6", "--seed", "7", CARD])
    assert out2 == out
    rc, _, err = run(["sample", "0", CARD])
    assert rc == 1 and "sample size" in err


if __name__ == "__main__":
    fails = 0
    tests = [(k, v) for k, v in sorted(globals().items()) if k.startswith("test_")]
    for name, fn in tests:
        try:
            fn()
            print("ok   " + name)
        except AssertionError as e:
            fails += 1
            print("FAIL %s: %s" % (name, e))
    print("test_mkwords: %d tests, %d failed" % (len(tests), fails))
    sys.exit(1 if fails else 0)
