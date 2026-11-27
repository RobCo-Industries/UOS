#!/usr/bin/env python3
"""Copy a staged card into the folder of `make bootdir`, safe for a card in use.

  syncbootdir.py STAGE DIR [--clean] [--replace] [--outside NAME]...
  syncbootdir.py guard DIR FOLDER...

guard stops when DIR is one of the FOLDERs, is inside one or holds one,
after resolving links. `make bootdir` runs it first, with its staging
folders, before it clears them.

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
- A symbolic link on the path of a stage file, a listed file or the
  list stops the run. The tool never follows or writes through a link.
  DIR itself is resolved once, so it may be a link.
- A file named by --outside is left alone: never stale, never removed,
  and its record stays. `make bootdir FIRMWARE=0` names the firmware.

Names compare without regard to case, as on a FAT32 card: the stage's
FOO.TXT and the card's foo.txt are one file. Every path goes through
one key, each part case-folded. The tool writes and names a file as
the stage spells it. It replaces a file through its existing entry,
then renames that entry to the stage's spelling, so no second entry
appears even where case matters. A folder keeps its spelling on the
card. Two names in DIR or in the stage with one key stop the run. The
list keeps one record per key.

Each file is written to UOSBOOT.TMP beside it, flushed and synced,
then moved into place with os.replace, and so is the list. A copy that
fails removes that file and leaves the old one as it was. A run removes
every UOSBOOT.TMP in DIR left by a run that stopped, once its checks
pass; it never treats that name as the terminal's data. A run that
stopped half way replaced some files and not the list: a listed file
whose bytes match the stage, not its record, is one that run wrote. It
takes the new record and is not named.

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
# The temporary name of a file being written. 8.3, one per folder.
TMP = "UOSBOOT.TMP"


def key(rel):
    """The one key every path comparison uses: each part case-folded."""
    return "/".join(part.casefold() for part in rel.split("/"))


def files_under(root):
    """The files under root by key, and the names that share a key."""
    out, twins = {}, set()
    for top, dirs, names in os.walk(root):
        rel = lambda n: os.path.relpath(os.path.join(top, n), root).replace(os.sep, "/")  # noqa: E731
        seen = {}
        for n in dirs + names:
            if n.casefold() in seen:
                twins |= {rel(n), rel(seen[n.casefold()])}
            seen[n.casefold()] = n
        # os.walk never enters a linked folder; name the link itself.
        for n in names + [x for x in dirs if os.path.islink(os.path.join(top, x))]:
            out[key(rel(n))] = rel(n)
    return out, twins


def digest(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def stop(msg, names):
    print(f"syncbootdir: {msg}:", file=sys.stderr)
    for n in sorted(names):
        print(f"  {n}", file=sys.stderr)
    sys.exit(1)


def entry(folder, name):
    """The name in folder whose key is that of name, or None."""
    try:
        names = os.listdir(folder)
    except (FileNotFoundError, NotADirectoryError):
        return None
    return next((n for n in names if n.casefold() == name.casefold()), None)


def on_card(root, rel):
    """The path of rel under root, spelled as the entries in root already
    are, and in the stage's spelling past the last existing one."""
    p = root
    for part in rel.split("/"):
        p = os.path.join(p, entry(p, part) or part)
    return p


def in_the_way(root, rel, kinds=True):
    """Why the path rel under root cannot be written as a file, or None.
    A symbolic link anywhere on the path is in the way: never follow one."""
    parts = rel.split("/")
    p = root
    for i, part in enumerate(parts):
        found = entry(p, part)
        if found is None:
            return None
        p = os.path.join(p, found)
        mode = os.lstat(p).st_mode
        here = "/".join(parts[:i + 1])
        if stat.S_ISLNK(mode):
            return f"{here}: a symbolic link"
        if not kinds:
            continue
        if i < len(parts) - 1 and not stat.S_ISDIR(mode):
            return f"{here}: a file where a folder is needed"
        if i == len(parts) - 1 and stat.S_ISDIR(mode):
            return f"{here}: a folder where a file goes"
    return None


def put(dst, write):
    """Write dst through TMP beside it: write(f), flush, sync, os.replace.
    On a failure TMP goes and dst stays as it was."""
    tmp = os.path.join(os.path.dirname(dst), TMP)
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0), 0o644)
    try:
        with os.fdopen(fd, "wb") as f:
            write(f)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, dst)
    except BaseException:
        try:
            os.remove(tmp)
        except FileNotFoundError:
            pass
        raise


def copy(src, dst):
    with open(src, "rb") as i:
        put(dst, lambda o: shutil.copyfileobj(i, o))


def read_list(path):
    """The records by key: (name, hash). One record per key, the last."""
    rec = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if line:
                h, _, name = line.partition("  ")
                rec[key(name)] = (name, h)
    return rec


def guard(dir_, folders):
    real = os.path.realpath(dir_)
    for f in folders:
        f = os.path.realpath(f)
        if os.path.commonpath([real, f]) in (real, f):
            print(f"syncbootdir: refusing DIR {dir_}: the build clears {f}", file=sys.stderr)
            sys.exit(1)


def main():
    if sys.argv[1:2] == ["guard"]:
        if len(sys.argv) < 4:
            print("usage: syncbootdir.py guard DIR FOLDER...", file=sys.stderr)
            sys.exit(2)
        guard(sys.argv[2], sys.argv[3:])
        return
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
    outside = {key(n) for n in a.outside}

    if a.dir == "/dev" or a.dir.startswith("/dev/"):
        print("syncbootdir: refusing a device path: this tool writes a folder", file=sys.stderr)
        sys.exit(1)
    # DIR itself may be reached through a link, such as a card's mount
    # name: resolve it once. Nothing under it is ever followed.
    a.dir = os.path.realpath(a.dir)
    if a.dir == "/dev" or a.dir.startswith("/dev/"):
        print("syncbootdir: refusing a device path: this tool writes a folder", file=sys.stderr)
        sys.exit(1)
    if os.path.exists(a.dir) and not os.path.isdir(a.dir):
        print(f"syncbootdir: {a.dir} exists and is not a folder", file=sys.stderr)
        sys.exit(1)

    want, twins = files_under(a.stage)
    if twins:
        stop("the stage holds names that differ only by case", twins)
    have, twins = files_under(a.dir) if os.path.isdir(a.dir) else ({}, set())
    if twins:
        stop("DIR holds names that differ only by case; a FAT32 card cannot", twins)
    temps = [have.pop(k) for k in list(have) if k.rsplit("/", 1)[-1] == TMP.casefold()]
    list_name = have.pop(key(LIST), LIST)
    list_path = os.path.join(a.dir, list_name)
    if os.path.isfile(list_path) and not os.path.islink(list_path):
        rec = read_list(list_path)
    else:
        if have or list_name != LIST:
            stop(f"{a.dir} is not empty and has no {LIST}; pick an empty DIR",
                 list(have.values()) + ([list_name] if list_name != LIST else []))
        rec = {}
    d = lambda k: os.path.join(a.dir, have[k])  # noqa: E731

    # Checks, before any write.
    blocked = {w for w in (in_the_way(a.dir, n) for n in list(want.values()) + [LIST]) if w}
    blocked |= {w for w in (in_the_way(a.dir, rec[k][0], kinds=False)
                            for k in set(rec) - outside) if w}
    blocked |= {f"{t}: a symbolic link" for t in temps if os.path.islink(os.path.join(a.dir, t))}
    # A folder with the temporary name, where this run writes a file, would
    # stop the run at that file, after earlier files were replaced.
    writes_in = {key(os.path.dirname(n)) for n in want.values()} | {""}
    for top, dirs, _ in os.walk(a.dir) if os.path.isdir(a.dir) else ():
        here = os.path.relpath(top, a.dir).replace(os.sep, "/")
        here = "" if here == "." else here
        if key(here) in writes_in:
            blocked |= {f"{(here + '/') if here else ''}{n}: a folder with the temporary name {TMP}"
                        for n in dirs if n.casefold() == TMP.casefold()}
    if blocked:
        stop("folders and files in DIR are in the way of this run", blocked)
    clash = {have[k] for k in set(want) & set(have) if k not in rec}
    if clash:
        stop("files in DIR that no run wrote clash with files of this run", clash)
    gone = {k for k in rec if k in have and k not in want and k not in outside}
    # Bytes that match the stage are what a stopped run wrote: not a change.
    changed = {k for k in (set(want) | gone) & set(have)
               if digest(d(k)) not in (rec[k][1], want.get(k) and digest(os.path.join(a.stage, want[k])))}
    stale = gone - changed
    if stale and not a.clean:
        stop("an earlier run wrote files this run does not; CLEAN=1 removes them",
             {have[k] for k in stale})

    for t in temps:
        os.remove(os.path.join(a.dir, t))
    new_rec = {k: r for k, r in rec.items() if k in have or k in outside}

    for k in sorted(gone & changed):
        print(f"syncbootdir: kept {have[k]}: changed since the last run")
    for k in sorted(stale):
        os.remove(d(k))
        del new_rec[k]
    for k in sorted(want, key=lambda k: want[k]):
        n = want[k]
        if k in changed and not a.replace:
            print(f"syncbootdir: kept {n}: changed since the last run")
            continue
        dst = on_card(a.dir, n)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        try:
            copy(os.path.join(a.stage, n), dst)
        except OSError as e:
            print(f"syncbootdir: writing {n} failed: {e}. Files before it are written;"
                  f" run again to finish.", file=sys.stderr)
            sys.exit(1)
        # Through the existing entry, then to the stage's spelling: a
        # rename, so no second entry appears where case matters.
        spelled = os.path.join(os.path.dirname(dst), os.path.basename(n))
        if dst != spelled:
            os.rename(dst, spelled)
        new_rec[k] = (n, digest(spelled))
    text = "".join(f"{h}  {n}\n" for n, h in sorted(new_rec.values())).encode()
    put(on_card(a.dir, LIST), lambda f: f.write(text))
    if os.path.basename(on_card(a.dir, LIST)) != LIST:
        os.rename(on_card(a.dir, LIST), os.path.join(a.dir, LIST))
    print(f"syncbootdir: {a.dir} is ready. Copy its files to the root of a FAT32 card.")


if __name__ == "__main__":
    main()
