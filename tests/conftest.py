import os
import sys

# RobCo-Core's harness: the --board option, the board fixture, need_mtools
# and robco_build. UOS adds --build-dir for the image under test, and
# --retros-img with --mfboot-sys for the whole-chain tests.
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CORE = os.path.join(ROOT, "external", "robco-core")
sys.path.insert(0, os.path.join(CORE, "tests"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
pytest_plugins = ["harness.plugin"]

import pytest  # noqa: E402


def pytest_addoption(parser):
    parser.addoption("--build-dir", default="build",
                     help="build directory, as BUILD in the Makefile")
    parser.addoption("--retros-img", default=None,
                     help="a RETROS.IMG for the whole-chain tests, as RETROS_IMG in the Makefile")
    parser.addoption("--mfboot-sys", default=None,
                     help="an MFBOOT.SYS for the whole-chain tests, as MFBOOT_SYS in the Makefile")


@pytest.fixture(scope="session")
def build_dir(request):
    return os.path.join(ROOT, request.config.getoption("--build-dir"))


@pytest.fixture(scope="session")
def uos_image(robco_build, build_dir):
    """build/uos.img, the image `make all` also copies to build/UOS.SYS."""
    return robco_build(ROOT, flags=(f"BUILD={build_dir}",),
                       image=os.path.join(build_dir, "uos.img"))


@pytest.fixture(scope="session")
def uos_bytes(uos_image):
    with open(uos_image, "rb") as f:
        return f.read()


@pytest.fixture(scope="session")
def hooks_image(robco_build, build_dir):
    """A test build with UOS_TEST_HOOKS=1: the maintenance port can fault and panic.
    Never shipped."""
    out = os.path.join(build_dir, "testhooks")
    flags = (f"BUILD={out}", "EXTRA_CFLAGS=-DUOS_TEST_HOOKS=1")
    return robco_build(ROOT, flags=flags, image=os.path.join(out, "uos.img"))


@pytest.fixture(scope="session")
def tick_fail_image(robco_build, build_dir):
    """A test build where tick_init() reports a failure. Never shipped."""
    out = os.path.join(build_dir, "tickfail")
    flags = (f"BUILD={out}", "EXTRA_CFLAGS=-DUOS_TEST_TICK_FAIL=1")
    return robco_build(ROOT, flags=flags, image=os.path.join(out, "uos.img"))


@pytest.fixture(scope="session")
def early_fault_image(robco_build, build_dir):
    """A test build with an undefined instruction just after the exception hook,
    the first point where a panic can happen. Never shipped."""
    out = os.path.join(build_dir, "earlyfault")
    flags = (f"BUILD={out}", "EXTRA_CFLAGS=-DUOS_TEST_EARLY_FAULT=1")
    return robco_build(ROOT, flags=flags, image=os.path.join(out, "uos.img"))


@pytest.fixture(scope="session")
def chain1_image(robco_build, build_dir):
    """RobCo-Core's examples/chain1 with a framebuffer and an amber console. It
    enters the image the harness puts at 0x00200000: a trampoline to UOS."""
    out = os.path.join(build_dir, "chain1-fb")
    return robco_build(os.path.join(CORE, "examples", "chain1"),
                       flags=(f"BUILD={out}", "EXTRA_CFLAGS=-DCHAIN1_FB=1"),
                       image=os.path.join(out, "chain1.img"))


@pytest.fixture(scope="session")
def chain1_bad_image(robco_build, build_dir):
    """examples/chain1 that breaks the boot info CRC before the jump."""
    out = os.path.join(build_dir, "chain1-bad")
    return robco_build(os.path.join(CORE, "examples", "chain1"),
                       flags=(f"BUILD={out}", "EXTRA_CFLAGS=-DCHAIN1_BAD_BOOTINFO=1"),
                       image=os.path.join(out, "chain1.img"))


@pytest.fixture(scope="session")
def chain_images(request):
    """(RETROS.IMG, MFBOOT.SYS) for the whole chain, or a skip."""
    retros = request.config.getoption("--retros-img")
    mfboot = request.config.getoption("--mfboot-sys")
    if not retros or not mfboot:
        pytest.skip("RETROS_IMG and MFBOOT_SYS are not both set: no whole-chain test")
    for path in (retros, mfboot):
        assert os.path.isfile(path), f"{path} is missing"
    return retros, mfboot
