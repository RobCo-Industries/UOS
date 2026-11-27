#!/usr/bin/env python3
"""Copy a staged card into the folder of `make bootdir`.

  syncbootdir.py STAGE DIR [--clean]

DIR/UOSBOOT.LST names every file a run wrote. A later run checks DIR
against it before it changes anything:

- DIR missing or empty: every file of STAGE is copied.
- A file in DIR that no run wrote: the run stops and names it. The tool
  never removes or replaces a file it did not write.
- A file an earlier run wrote that STAGE no longer has: the run stops
  and names it. --clean removes it.
- Otherwise every file of STAGE replaces the one in DIR, edited or not.

Exit codes: 0 done, 1 stopped, 2 bad usage.
"""

import argparse
import os
import shutil
import sys

LIST = "UOSBOOT.LST"


def files_under(root):
    out = set()
    for top, dirs, names in os.walk(root):
        dirs.sort()
        for n in names:
            out.add(os.path.relpath(os.path.join(top, n), root).replace(os.sep, "/"))
    return out


def stop(msg, names):
    print(f"syncbootdir: {msg}:", file=sys.stderr)
    for n in sorted(names):
        print(f"  {n}", file=sys.stderr)
    sys.exit(1)


def main():
    ap = argparse.ArgumentParser(description="Copy a staged card into a boot folder.")
    ap.add_argument("stage")
    ap.add_argument("dir")
    ap.add_argument("--clean", action="store_true",
                    help="remove files an earlier run wrote that this run does not")
    a = ap.parse_args()

    if a.dir.startswith("/dev/") or a.dir == "/dev":
        print("syncbootdir: refusing a device path: this tool writes a folder", file=sys.stderr)
        sys.exit(1)
    if os.path.exists(a.dir) and not os.path.isdir(a.dir):
        print(f"syncbootdir: {a.dir} exists and is not a folder", file=sys.stderr)
        sys.exit(1)

    want = files_under(a.stage)
    have = files_under(a.dir) if os.path.isdir(a.dir) else set()
    listed = set()
    list_path = os.path.join(a.dir, LIST)
    if os.path.isfile(list_path):
        with open(list_path, encoding="ascii") as f:
            listed = {line.strip() for line in f if line.strip()}
    have.discard(LIST)

    foreign = have - listed
    if foreign:
        stop(f"{a.dir} holds files no earlier run wrote; move them or pick an empty DIR",
             foreign)
    stale = (listed & have) - want
    if stale and not a.clean:
        stop("an earlier run wrote files this run does not; CLEAN=1 removes them", stale)

    for n in stale:
        os.remove(os.path.join(a.dir, n))
    for n in sorted(want):
        dst = os.path.join(a.dir, n)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(os.path.join(a.stage, n), dst)
    with open(list_path, "w", encoding="ascii") as f:
        f.write("".join(n + "\n" for n in sorted(want)))
    print(f"syncbootdir: {a.dir} is ready. Copy its files to the root of a FAT32 card.")


if __name__ == "__main__":
    main()
