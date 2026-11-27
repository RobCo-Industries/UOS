"""The workflow guard is part of what CI runs, and it judges a workflow.

`make check-workflows` comes from the library's mk/robco.mk and reads this
repository's .github/workflows/. `check-pins` depends on it, and CI runs
`check-pins` in `test-host`. The guard's own rules are tested in the
library: here two bad files only prove the tool this repository calls is
the one that refuses them.
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOOL = os.path.join(ROOT, "external", "robco-core", "tools", "check_workflow_exprs.py")
SHA = "0123456789abcdef0123456789abcdef01234567"

GOOD = f"""name: good
on: push
jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@{SHA} # v4.2.2
      - env:
          REF: ${{{{ github.ref }}}}
        run: echo "$REF"
"""

BAD_RUN = """name: bad
on: push
jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - run: echo ${{ github.ref }}
"""

BAD_PIN = """name: bad
on: push
jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
"""


def env():
    return {"PATH": os.environ.get("PATH", ""), "HOME": os.environ.get("HOME", "")}


def make(*args):
    return subprocess.run(["make", "-C", ROOT, *args], env=env(), capture_output=True, text=True)


def guard(tmp_path, text):
    f = tmp_path / "ci.yml"
    f.write_text(text)
    return subprocess.run([sys.executable, "-I", TOOL, str(f)], env=env(),
                          capture_output=True, text=True)


def test_the_repository_workflows_pass():
    r = make("-s", "check-workflows")
    assert r.returncode == 0, r.stdout + r.stderr
    assert "overriding" not in r.stderr, r.stderr


def test_ci_runs_the_guard():
    for goal in ("check-pins", "test-host"):
        r = make("-n", goal)
        assert r.returncode == 0, r.stderr
        assert "check_workflow_exprs.py" in r.stdout, goal


def test_good_workflow_passes(tmp_path):
    r = guard(tmp_path, GOOD)
    assert r.returncode == 0, r.stdout + r.stderr


def test_expression_in_run_fails(tmp_path):
    assert guard(tmp_path, BAD_RUN).returncode != 0


def test_unpinned_action_fails(tmp_path):
    assert guard(tmp_path, BAD_PIN).returncode != 0
