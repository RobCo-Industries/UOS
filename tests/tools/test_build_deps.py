"""The image build tracks headers whatever the spelling of BUILD.

The QEMU fixtures pass an absolute BUILD, `make all` a relative one. gcc
writes each .d file with the object named as the compile rule spelled it.
When the two spellings differ, make never matches the .d to the target, so
a header change does not rebuild the object and an image links a stale one.
"""
import os
import shutil
import subprocess
import time

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
REL = "build/deptest"
OBJ = "obj/src/shell/shrules.o"
SRC = os.path.join(ROOT, "src", "shell", "shrules.c")
HDR = os.path.join(ROOT, "src", "shell", "shrules.h")


def make(*args):
    env = {"PATH": os.environ.get("PATH", ""), "HOME": os.environ.get("HOME", "")}
    return subprocess.run(["make", "-s", "-C", ROOT, "CI=1", *args], env=env,
                          stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)


@pytest.fixture
def deptest():
    if shutil.which("arm-none-eabi-gcc") is None:
        pytest.skip("no cross compiler")
    out = os.path.join(ROOT, REL)
    shutil.rmtree(out, ignore_errors=True)
    yield out
    shutil.rmtree(out, ignore_errors=True)


@pytest.mark.parametrize("first, second", [("rel", "abs"), ("abs", "rel")])
def test_header_change_rebuilds_object(deptest, first, second):
    spell = {"rel": REL, "abs": deptest}
    r = make(f"BUILD={spell[first]}", f"{REL}/{OBJ}")
    assert r.returncode == 0, r.stderr
    obj = os.path.join(deptest, OBJ)
    # The .d names the object as the rule that reads it will.
    with open(obj[:-2] + ".d") as f:
        assert f.readline().startswith(f"{REL}/{OBJ}:")
    # The object is newer than its source and older than a header it
    # includes, as after an edit of that header. Times only move forward.
    t = max(os.stat(SRC).st_mtime, time.time() - 10)
    os.utime(obj, (t, t))
    now = max(time.time(), t + 1)
    os.utime(HDR, (now, now))
    # make -q: 0 when the target is up to date, 1 when it would rebuild.
    r = make("-q", f"BUILD={spell[second]}", f"{REL}/{OBJ}")
    assert r.returncode == 1, "the header change did not reach the object"
