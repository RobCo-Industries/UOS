"""`make check-workflows` fails on an unsafe workflow and passes a safe one.

The test writes workflow files to a temp folder and points WORKFLOW_DIR at
it, so the real .github/ folder stays untouched.
"""
import os
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
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


def check(tmp_path, text):
    (tmp_path / "ci.yml").write_text(text)
    env = {"PATH": os.environ.get("PATH", ""), "HOME": os.environ.get("HOME", "")}
    return subprocess.run(["make", "-s", "-C", ROOT, "check-workflows",
                           f"WORKFLOW_DIR={tmp_path}"], env=env,
                          capture_output=True, text=True)


def test_good_workflow_passes(tmp_path):
    r = check(tmp_path, GOOD)
    assert r.returncode == 0, r.stdout + r.stderr


def test_expression_in_run_fails(tmp_path):
    assert check(tmp_path, BAD_RUN).returncode != 0


def test_unpinned_action_fails(tmp_path):
    assert check(tmp_path, BAD_PIN).returncode != 0
