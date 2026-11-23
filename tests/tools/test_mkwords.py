"""Tests for tools/mkwords.py. Run with pytest, or directly with python3."""
import contextlib
import io
import os
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
    bad_case(4, list(reversed(base())), "not sorted")


def test_too_few():
    bad_case(4, base()[:63], "63 words, want at least 64")


def test_at_minimum_ok():
    d = make_dir()
    try:
        write(d, 4, base()[:64], crlf=True)
        rc, _, err = run(["check", d])
        assert rc == 0, err
    finally:
        shutil.rmtree(d)


def test_deny_list():
    bad_case(4, sorted(base() + ["KILL"]), "deny list")


def test_missing_file():
    d = make_dir()
    try:
        os.remove(os.path.join(d, "W07.TXT"))
        rc, _, err = run(["check", d])
        assert rc == 1 and "missing" in err
    finally:
        shutil.rmtree(d)


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
