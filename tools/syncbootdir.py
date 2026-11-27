#!/usr/bin/env python3
"""Copy a staged card into the folder of `make bootdir`, safe for a card in use.

  syncbootdir.py STAGE DIR [--clean] [--replace] [--outside NAME]...

DIR/UOSBOOT.LST has one line per file a run wrote: the SHA-256 of the
bytes written, two spaces and the path, as `shasum -a 256` prints it.

- No list: DIR must be empty or missing, or the run stops.
- A stage file not in DIR is written.
- A stage file in DIR with the bytes the list records is replaced.
- A stage file in DIR with other bytes was changed since the last run:
  it is kept and named, and its old record stays. --replace writes it.
- A stage file in DIR that the list does not name stops the run.
- A file no run wrote that the stage does not hold is left alone.
- A listed file with other bytes is always kept, named and keeps its
  record, whether the stage holds it or not. It never stops a run.
- A listed file the stage no longer holds, with the bytes the list
  records, stops the run. --clean removes it.
- A file where a stage file needs a folder, or a folder where a stage
  file goes, stops the run.
- A file named by --outside is left alone: never stale, never removed,
  and its record stays. `make bootdir FIRMWARE=0` names the firmware.

Every check comes before the first write. No folder is ever removed.
Exit codes: 0 done, 1 stopped, 2 bad usage.
"""

import argparse
import hashlib
import os
import shutil
import stat
import sys

LIST = "UOSBOOT.LST"


def files_under(root):
    out = set()
    for top, _, names in os.walk(root):
        for n in names:
            out.add(os.path.relpath(os.path.join(top, n), root).replace(os.sep, "/"))
    return out


def digest(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def stop(msg, names):
    print(f"syncbootdir: {msg}:", file=sys.stderr)
    for n in sorted(names):
        print(f"  {n}", file=sys.stderr)
    sys.exit(1)


def in_the_way(root, rel):
    """Why the path rel under root cannot be written as a file, or None."""
    parts = rel.split("/")
    p = root
    for i, part in enumerate(parts):
        p = os.path.join(p, part)
        try:
            mode = os.lstat(p).st_mode
        except FileNotFoundError:
            return None
        here = "/".join(parts[:i + 1])
        if i < len(parts) - 1 and not stat.S_ISDIR(mode):
            return f"{here}: a file where a folder is needed"
        if i == len(parts) - 1 and stat.S_ISDIR(mode):
            return f"{here}: a folder where a file goes"
    return None


def read_list(path):
    rec = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if line:
                h, _, name = line.partition("  ")
                rec[name] = h
    return rec


def main():
    ap = argparse.ArgumentParser(description="Copy a staged card into a boot folder.")
    ap.add_argument("stage")
    ap.add_argument("dir")
    ap.add_argument("--clean", action="store_true",
                    help="remove unchanged files an earlier run wrote that this run does not")
    ap.add_argument("--replace", action="store_true",
                    help="also replace files changed since the last run")
    ap.add_argument("--outside", action="append", default=[], metavar="NAME",
                    help="a file this run leaves alone, with its record")
    a = ap.parse_args()
    outside = set(a.outside)

    if a.dir == "/dev" or a.dir.startswith("/dev/"):
        print("syncbootdir: refusing a device path: this tool writes a folder", file=sys.stderr)
        sys.exit(1)
    if os.path.exists(a.dir) and not os.path.isdir(a.dir):
        print(f"syncbootdir: {a.dir} exists and is not a folder", file=sys.stderr)
        sys.exit(1)

    d = lambda n: os.path.join(a.dir, n)  # noqa: E731
    want = files_under(a.stage)
    have = files_under(a.dir) if os.path.isdir(a.dir) else set()
    list_path = d(LIST)
    if os.path.isfile(list_path):
        rec = read_list(list_path)
        have.discard(LIST)
    else:
        if have:
            stop(f"{a.dir} is not empty and has no {LIST}; pick an empty DIR", have)
        rec = {}

    # Checks, before any write.
    blocked = {w for w in (in_the_way(a.dir, n) for n in want | {LIST}) if w}
    if blocked:
        stop("folders and files in DIR are in the way of this run", blocked)
    clash = {n for n in want & have if n not in rec}
    if clash:
        stop("files in DIR that no run wrote clash with files of this run", clash)
    gone = {n for n in rec if n in have and n not in want and n not in outside}
    changed = {n for n in (want | gone) & have if digest(d(n)) != rec[n]}
    stale = gone - changed
    if stale and not a.clean:
        stop("an earlier run wrote files this run does not; CLEAN=1 removes them", stale)

    new_rec = {n: h for n, h in rec.items() if n in have or n in outside}

    for n in sorted(gone & changed):
        print(f"syncbootdir: kept {n}: changed since the last run")
    for n in sorted(stale):
        os.remove(d(n))
        del new_rec[n]
    for n in sorted(want):
        if n in changed and not a.replace:
            print(f"syncbootdir: kept {n}: changed since the last run")
            continue
        os.makedirs(os.path.dirname(d(n)), exist_ok=True)
        shutil.copyfile(os.path.join(a.stage, n), d(n))
        new_rec[n] = digest(d(n))
    with open(list_path, "w", encoding="utf-8") as f:
        f.write("".join(f"{new_rec[n]}  {n}\n" for n in sorted(new_rec)))
    print(f"syncbootdir: {a.dir} is ready. Copy its files to the root of a FAT32 card.")


if __name__ == "__main__":
    main()
