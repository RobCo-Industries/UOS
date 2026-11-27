"""The committed sound tables against the library's generator and checker.

src/sfx/sfx_table.c and .h come from src/sfx/sfx_models.json through the
library's tools/sfx_table.py. `make sfx-table-check` compares only the
model's hash before each build. Here the generator renders every table
again and compares bytes, and tools/sfx_check.py measures the dumped
tables against what the model predicts.
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CORE = os.environ.get("ROBCO_CORE", os.path.join(ROOT, "external", "robco-core"))
TOOLS = os.path.join(CORE, "tools")
MODEL = os.path.join(ROOT, "src", "sfx", "sfx_models.json")
TABLE = ["--c", os.path.join(ROOT, "src", "sfx", "sfx_table.c"),
         "--h", os.path.join(ROOT, "src", "sfx", "sfx_table.h"), "--prefix", "uos_sfx_"]
SOUNDS = ["key_a", "key_b", "char_scroll", "pass_good", "pass_bad", "fan"]


def tool(name, *args):
    return subprocess.run([sys.executable, os.path.join(TOOLS, name), *args],
                          capture_output=True, text=True, timeout=150)


def test_table_matches_model_bytes():
    r = tool("sfx_table.py", MODEL, *TABLE, "--check", "--full")
    assert r.returncode == 0, r.stdout + r.stderr


def test_dumped_tables_pass_the_checker(tmp_path):
    r = tool("sfx_table.py", MODEL, "--dump", str(tmp_path))
    assert r.returncode == 0, r.stdout + r.stderr
    assert sorted(os.listdir(tmp_path)) == sorted(s + ".u8" for s in SOUNDS)
    r = tool("sfx_check.py", MODEL, str(tmp_path))
    assert r.returncode == 0, "sfx_check.py failed:\n" + r.stdout + r.stderr
    lines = r.stdout.splitlines()
    assert len(lines) == len(SOUNDS), r.stdout
    for name, line in zip(SOUNDS, lines):
        assert line.startswith(name + " ") and line.endswith(": ok"), r.stdout
