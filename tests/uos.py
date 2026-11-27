"""Helpers for the UOS target tests."""

import contextlib
import os
import re
import struct
import subprocess
import tempfile
import time

from PIL import Image

from harness.boot import SCRIPT_DATA, SCRIPT_PORT, boot, is_script_status, parse_markers
from harness.env import minimal_env
from harness.sdimage import make_sd_image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RIMG = os.path.join(ROOT, "external", "robco-core", "tools", "rimg.py")
OS_SLOT = 0x00400000
AGENT_SLOT = 0x00200000
TIMEOUT = 60
GOLDEN_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "golden")
CFG = "/UOS/UOS.CFG"
SYS = "/ROBCO/UOS.SYS"

# The tick rate of the kernel, UOS_TICK_HZ in src/kernel/uos.h.
TICK_HZ = 100

# Bring-up markers in order, docs/ARCHITECTURE.md.
# One "service" marker per service of src/kernel/services.c, in table order.
SERVICES = ["sound", "uptime", "vfs", "console", "session", "shell", "devices", "holo", "hack", "menu"]
# The maintenance commands each service registers, test builds included.
SERVICE_COMMANDS = {"uptime": ["uptime"], "vfs": ["fs"], "console": ["readline", "page", "osk", "button"],
                    "session": ["sess"], "shell": ["sh"], "devices": ["dev"],
                    "holo": ["holo"], "hack": ["hack"], "menu": ["menu"], "sound": []}
BOOT_ORDER = ["boot", "handoff", "maint", "board", "mbox", "mmu", "screen", "tick", "heap",
              "input", "selfcheck", "mount", "config", "buttons", "rtc", "sound"] + \
             ["service"] * len(SERVICES) + ["ready"]

READY = re.compile(r"@RC stage=uos ev=ready [^\n]*\n")
PANIC = re.compile(r"@RC stage=uos ev=panic [^\n]*\n")
IDLE = re.compile(r"@RC stage=uos ev=idle\r?\n")

# ldr pc, [pc, #-4]; .word OS_SLOT. Registers r0 to r3 pass through unchanged.
TRAMPOLINE = struct.pack("<II", 0xE51FF004, OS_SLOT)

# The default settings the tests use.
DEFAULT_CFG = b"[system]\nname = TEST TERMINAL\n"


def events(vm, name, stage="uos"):
    return [m for m in vm.markers() if m.get("stage") == stage and m.get("ev") == name]


def one(vm, name, stage="uos"):
    found = events(vm, name, stage)
    assert len(found) == 1, f"{stage} {name}: {len(found)} found\n{vm.serial}"
    return found[0]


def order(vm, stage="uos"):
    return [m["ev"] for m in vm.markers() if m.get("stage") == stage]


def card(tmp_path, files, name="sd.img"):
    """An SD card image with `files` ({"/UOS/X": bytes, "/UOS/": b"" for a folder})."""
    content = {k.lstrip("/"): v for k, v in files.items()}
    if not content:
        content["EMPTY/"] = b""
    sd = tmp_path / name
    make_sd_image(sd, 16, content)
    return sd


def uos_card(tmp_path, cfg=DEFAULT_CFG, extra=None, name="sd.img"):
    """A card with a /UOS tree and UOS.CFG (None: the folder without the file)."""
    files = {"/UOS/": b""} if cfg is None else {CFG: cfg}
    files.update(extra or {})
    return card(tmp_path, files, name=name)


@contextlib.contextmanager
def alone(image, board, sd=None, extra_args=None, qtest=False, gpio_high=()):
    """Boots UOS with no earlier stage: QEMU loads it at its link address, and
    a two-word image at 0x8000 jumps to it with no boot info in r3. qtest=True
    lets a test drive device inputs with vm.qmp.set_irq_in(). gpio_high lists
    Pi pins driven high before the guest starts; it needs qtest=True."""
    with tempfile.TemporaryDirectory(prefix="uos") as tmp:
        tramp = os.path.join(tmp, "tramp.img")
        with open(tramp, "wb") as f:
            f.write(TRAMPOLINE)
        args = ["-device", f"loader,file={image},addr={OS_SLOT:#x},force-raw=on"]
        with boot(tramp, board, sd=sd, serial2=True,
                  extra_args=args + list(extra_args or []), qtest=qtest,
                  gpio_high=gpio_high,
                  elf=os.path.splitext(image)[0] + ".elf") as vm:
            yield vm


def agent_tramp(path):
    """A sealed BOOT image for 0x00200000 that jumps to UOS. chain1 validates and
    enters it with its boot info in r3, which the jump keeps."""
    payload = path + ".bin"
    with open(payload, "wb") as f:
        f.write(TRAMPOLINE)
    subprocess.run(["python3", RIMG, "pack", payload, "-o", path, "--magic", "BOOT",
                    "--subtype", "4", "--load-addr", f"{AGENT_SLOT:#x}", "--abi-version", "2.0",
                    "--name", "TRAMP"],
                   check=True, capture_output=True, env=minimal_env())
    return path


@contextlib.contextmanager
def after_chain1(chain1, image, board, sd=None):
    """Boots RobCo-Core's examples/chain1 from 0x8000. It enters a trampoline at
    0x00200000, which jumps to UOS at 0x00400000 with chain1's boot info."""
    with tempfile.TemporaryDirectory(prefix="uos") as tmp:
        tramp = agent_tramp(os.path.join(tmp, "tramp.img"))
        args = ["-device", f"loader,file={tramp},addr={AGENT_SLOT:#x},force-raw=on",
                "-device", f"loader,file={image},addr={OS_SLOT:#x},force-raw=on"]
        with boot(chain1, board, sd=sd, serial2=True, extra_args=args,
                  elf=os.path.splitext(image)[0] + ".elf") as vm:
            yield vm


@contextlib.contextmanager
def after_chain(retros, board, sd, qtest=False):
    """Boots the whole chain from a card that holds MFBOOT.SYS and UOS.SYS.
    qtest=True opens QEMU's test port, for PiGpio pin reads."""
    with boot(retros, board, sd=sd, serial2=True, qtest=qtest) as vm:
        yield vm


def wait_ready(vm):
    return vm.wait_for(READY, timeout=TIMEOUT)


def answer(vm, command):
    """One maintenance command; asserts "ok" and returns the "| " lines."""
    reply = vm.script(command, timeout=TIMEOUT)
    assert reply.status == "ok", (command, reply)
    return reply.lines


# Characters the screens ignore, sent before a key in an ordering test. The
# loop takes one input event per poll, so the key and a command sent after it
# reach the loop while these still fill the input layer: a call that jumps the
# input queue then runs before the key every time, not only on a loaded host.
FILLER = "text " + "z" * 20


def batch(vm, commands, timeout=TIMEOUT):
    """Several maintenance commands in one port write. Returns one ScriptReply-like
    (status, lines) pair per command, in order: the port answers the commands one by
    one, so the n-th status line belongs to the n-th command."""
    if any("\n" in c or "\r" in c for c in commands):
        raise ValueError("one command per entry, without a line ending")
    start = len(vm.lines(SCRIPT_PORT))
    vm.send("".join(c + "\n" for c in commands), port=SCRIPT_PORT)
    deadline = time.monotonic() + timeout
    while True:
        new = vm.lines(SCRIPT_PORT)[start:]
        out, data = [], []
        for line in new:
            if is_script_status(line):
                out.append((line, data))
                data = []
            elif line.startswith(SCRIPT_DATA):
                data.append(line[len(SCRIPT_DATA):])
        if len(out) >= len(commands):
            assert len(out) == len(commands), (commands, new)
            return out
        left = deadline - time.monotonic()
        assert left > 0, (commands, new)
        # Woken by the reader thread when a byte arrives; no fixed sleep.
        with vm._cond:
            vm._cond.wait(min(left, 0.2))


def state(vm):
    lines = answer(vm, "state")
    assert len(lines) == 1, lines
    return dict(field.split("=", 1) for field in lines[0].split())


def dump(vm):
    """The screen text from the maintenance port: (header marker, rows)."""
    lines = answer(vm, "dump")
    head = parse_markers(lines[0] + "\n")[0]
    rows = [line[1:] for line in lines if line.startswith("|")]
    return head, rows


def key(vm, name):
    """Presses and releases one key."""
    answer(vm, f"key {name}")


def timers(vm):
    """The active timers: [{"id": ..., "due": ..., "period": ...}]."""
    lines = answer(vm, "timers")
    head = dict(f.split("=", 1) for f in lines[0].split()[1:])
    rows = [dict(f.split("=", 1) for f in line.split()[1:]) for line in lines[1:]]
    assert int(head["count"]) == len(rows), lines
    return rows


def screenshot(vm, tmp_path, name="screen"):
    ppm = tmp_path / f"{name}.ppm"
    vm.screendump(ppm)
    with Image.open(ppm) as img:
        return img.convert("RGB")


def check_golden(rows, name):
    """Compares screen rows with tests/golden/<name>.txt. ROBCO_UPDATE_GOLDEN=1 rewrites it."""
    path = os.path.join(GOLDEN_DIR, f"{name}.txt")
    text = "\n".join(r.rstrip() for r in rows).rstrip("\n") + "\n"
    if os.environ.get("ROBCO_UPDATE_GOLDEN") == "1":
        os.makedirs(GOLDEN_DIR, exist_ok=True)
        with open(path, "w", encoding="ascii") as f:
            f.write(text)
    with open(path, encoding="ascii") as f:
        assert f.read() == text, f"screen differs from {path}:\n{text}"


# ---- File system service helpers -------------------------------------------

FS_FINAL = re.compile(r"@RC stage=uos ev=fs op=(\w+) rc=")


def fs(vm, command, timeout=TIMEOUT):
    """Runs `fs <command>` on the maintenance port, then waits for the
    "ev=fs op=<op> rc=..." marker that ends it. Returns (final, items):
    the final marker and the "ev=fs" markers before it, such as listing
    entries or files deleted."""
    import time
    op = command.split()[0]
    start = len(vm.serial)
    answer(vm, "fs " + command)
    deadline = time.monotonic() + timeout
    while True:
        marks = [m for m in parse_markers(vm.serial[start:]) if m.get("ev") == "fs"]
        for i, m in enumerate(marks):
            if "rc" in m and m.get("op") == op:
                return m, [x for x in marks[:i] if "rc" not in x]
        assert time.monotonic() < deadline, f"no fs {op} result:\n{vm.serial[start:]}"
        time.sleep(0.05)


def card_tree(extra=None):
    """The default card/ tree as card() files, plus `extra`."""
    files = {}
    base = os.path.join(ROOT, "card")
    for dirpath, _, names in os.walk(base):
        for n in names:
            path = os.path.join(dirpath, n)
            with open(path, "rb") as f:
                files["/" + os.path.relpath(path, base).replace(os.sep, "/")] = f.read()
    files.update(extra or {})
    return files


def _mtools(sd, *args):
    env = minimal_env(MTOOLS_SKIP_CHECK="1")
    img = f"{os.fspath(sd)}@@{2048 * 512}"
    return subprocess.run([args[0], "-i", img] + list(args[1:]), check=True, env=env,
                          capture_output=True, text=True).stdout


def card_free(sd):
    """Free bytes on the card's FAT volume, from mtools."""
    m = re.search(r"([\d ]+) bytes free", _mtools(sd, "mdir", "::/"))
    return int(m.group(1).replace(" ", ""))


def card_put(sd, name, data, tmp_path):
    """Copies `data` to `name` ("UOS/X.BIN") on the card with mtools."""
    src = tmp_path / "put.bin"
    src.write_bytes(data)
    _mtools(sd, "mcopy", "-o", os.fspath(src), "::/" + name)


def card_names(sd, folder):
    """The short names in one folder of the card, upper-case, with "." between parts."""
    out = _mtools(sd, "mdir", "-b", "::/" + folder)
    return sorted(line.rstrip("/").rsplit("/", 1)[-1].upper() for line in out.splitlines()
                  if line.strip())

def wait_marker(vm, prefix, n=1):
    """Waits for the n-th marker that starts with "ev=<prefix>" and returns it as a dict."""
    one_line = r"@RC stage=uos ev=" + re.escape(prefix) + r"(?:[ \r][^\n]*)?\n"
    vm.wait_for(re.compile(r"(?:" + one_line + r"[\s\S]*?){" + str(n) + r"}"), timeout=TIMEOUT)
    head = prefix.split()
    found = [m for m in vm.markers() if m.get("stage") == "uos" and m.get("ev") == head[0]
             and all(m.get(k) == v for k, v in (f.split("=", 1) for f in head[1:]))]
    assert len(found) >= n, (prefix, n, found)
    return found[n - 1]


def dump_until(vm, check, timeout=TIMEOUT):
    """Dumps the screen until check(rows) is true, and returns the rows. Input
    from the port reaches the screens a loop pass after its answer, so a
    test that just sent keys asks again until the screen shows them."""
    import time

    deadline = time.monotonic() + timeout
    while True:
        _, rows = dump(vm)
        rows = [r.rstrip() for r in rows]
        if check(rows):
            return rows
        assert time.monotonic() < deadline, "\n".join(rows)
        time.sleep(0.05)
