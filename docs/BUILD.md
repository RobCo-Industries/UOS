# Building UOS

Nothing here is validated on real hardware.

## Tools

- `arm-none-eabi-gcc` and binutils
- GNU make and Python 3
- For the tests: `qemu-system-arm`, mtools and a Python venv, which `make venv` creates

The library lives in the submodule `external/robco-core`. Run `git submodule update --init` after a clone.

## Targets

| Target | What it does |
|---|---|
| `make` or `make all` | Builds `build/uos.img`, a sealed rimg, and copies it to `build/UOS.SYS` |
| `make test-host` | Checks the CI pins and doc links, then builds and runs the host tests |
| `make test-target BOARD=raspi2b` | Runs the QEMU tests on one board. `BOARD=all` is the default. |
| `make test` | `test-host`, then `test-target` |
| `make test-tools` | Runs pytest over `tests/tools/` |
| `make sdimage` | Writes `build/sd.img` with `/ROBCO/UOS.SYS` and the `card/` tree |
| `make run BOARD=raspi2b` | Boots `build/sd.img` in QEMU on this terminal. `WINDOW=1` opens the display. |
| `make check-pins` | Checks that the CI workflow pins match the submodule, and the doc links |
| `make sfx-table` | Writes `src/sfx/sfx_table.c` and `.h` from `src/sfx/sfx_models.json` with the library's `tools/sfx_table.py`. Commit both files. |
| `make sfx-table-check` | Checks that the committed table matches the model's hash and the generator's revision. Every image build, `make`, `make sdimage` and `make run` included, runs it before it compiles the table and stops on a stale one. A passed check leaves the stamp `build/sfx-table.ok` and runs again only when the model, the table or the generator changes. |
| `make ci-local` | Runs the CI build job, `make CI=1 all test-host`, in the robco-ci container. `CI_LOCAL_CPUS` caps the CPUs, 4 by default |
| `make ci-local-image` | Builds the robco-ci image locally from the library's `ci/Dockerfile` |
| `make clean` | Removes `build/` |

`CI=1` adds `-Werror`, as CI does.

## The image

- A sealed rimg with magic `BOOT`, subtype 0, ABI 2.
- Linked at `0x00400000`, the OS slot. The link fails if the image passes `0x01000000`.
- The stage name in markers is `uos`.

## The whole chain on one card

```sh
make sdimage CHAIN=1 RETROS_IMG=path/to/RETROS.IMG MFBOOT_SYS=path/to/MFBOOT.SYS
```

This adds RETROS-BIOS as the first stage with its `config.txt`, `/ROBCO/MFBOOT.SYS`, and a `/ROBCO/BOOT.CFG` whose only entry is `/ROBCO/UOS.SYS` with a 3 second timeout. `make run CHAIN=1 RETROS_IMG=...` boots it.

## Where module sources go

The Makefile finds sources by wildcard. A module branch only adds files:

- Every `src/<dir>/*.c` builds into the image.
- The pure files the design names are picked up when they exist: `src/dcl/*.c`, `src/dev/devcfg.c`, `src/dev/devstate.c`, `src/hack/hack.c`, `src/holo/holofmt.c`, `src/holo/holoread.c`, `src/osk/osk.c`, `src/session/lockout.c`, `src/session/sha256.c`, `src/session/uaf.c`, `src/tui/pager.c`, `src/tui/tui.c`, `src/tui/tuicfg.c`, `src/vfs/path.c`. Another pure file needs one line in `PURE_NAMED` or `PURE_KERNEL`.
- Every `tests/host/test_*.c` is a host test. It links all pure files and the library's `src/lib/`.
- Every `src/<dir>/` is an include directory for the image and the host tests.
- A generated header lands in `build/host/gen/` before any host test builds: `uaf_vectors.h`, from `tests/vectors/uaf.json`.
- `make test-tools` builds the host tools the tool tests call, such as `build/host/holocheck`, then runs pytest over `tests/tools/`.
