"""Helpers for the UOS target tests."""

import contextlib
import os
import re
import struct
import subprocess
import tempfile

from PIL import Image

from harness.boot import boot, parse_markers
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
BOOT_ORDER = ["boot", "handoff", "maint", "board", "mbox", "screen", "tick", "heap", "input",
              "selfcheck", "mount", "config", "rtc", "ready"]

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
def alone(image, board, sd=None, extra_args=None):
    """Boots UOS with no earlier stage: QEMU loads it at its link address, and
    a two-word image at 0x8000 jumps to it with no boot info in r3."""
    with tempfile.TemporaryDirectory(prefix="uos") as tmp:
        tramp = os.path.join(tmp, "tramp.img")
        with open(tramp, "wb") as f:
            f.write(TRAMPOLINE)
        args = ["-device", f"loader,file={image},addr={OS_SLOT:#x},force-raw=on"]
        with boot(tramp, board, sd=sd, serial2=True,
                  extra_args=args + list(extra_args or []),
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
def after_chain(retros, board, sd):
    """Boots the whole chain from a card that holds MFBOOT.SYS and UOS.SYS."""
    with boot(retros, board, sd=sd, serial2=True) as vm:
        yield vm


def wait_ready(vm):
    return vm.wait_for(READY, timeout=TIMEOUT)


def answer(vm, command):
    """One maintenance command; asserts "ok" and returns the "| " lines."""
    reply = vm.script(command, timeout=TIMEOUT)
    assert reply.status == "ok", (command, reply)
    return reply.lines


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
