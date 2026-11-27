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
| `make bootdir DIR=<dir> RETROS_IMG=... MFBOOT_SYS=...` | Writes the files of a real card for the whole chain into a folder, see [A real card](#a-real-card) |
| `make run BOARD=raspi2b` | Boots `build/sd.img` in QEMU on this terminal. `WINDOW=1` opens the display. |
| `make check-pins` | Checks that the CI workflow pins match the submodule, and the doc links. It runs `check-workflows` too. |
| `make check-workflows` | Runs the library's workflow guard on `.github/workflows/`. It fails on a `${{ }}` inside a `run:` block and on an action not pinned by a full commit. CI runs it through `test-host`. |
| `make sfx-table` | Writes `src/sfx/sfx_table.c` and `.h` from `src/sfx/sfx_models.json` with the library's `tools/sfx_table.py`. Commit both files. |
| `make sfx-table-check` | Checks that the committed table matches the model's hash and the generator's revision. Every image build, `make`, `make sdimage` and `make run` included, runs it before it compiles the table and stops on a stale one. A passed check leaves the stamp `build/sfx-table.ok` and runs again only when the model, the table or the generator changes. |
| `make sfx-preview BOARD=raspi0` | Boots a session with the sound on in QEMU and renders its sounds into `build/sfx_preview.wav`. `BOARD` is raspi2b by default. See [the session's sounds](TESTING.md#the-sessions-sounds). |
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

Three more variables give the card the boot agent's other images and a full config:

| Variable | Effect |
|---|---|
| `MAINT_SYS=<file>` | Copies the maintenance image to `/ROBCO/MAINT.SYS` |
| `DIAG_SYS=<file>` | Copies the diagnostics image to `/ROBCO/DIAG.SYS` |
| `BOOT_CFG=<file>` | With `CHAIN=1`, copies the file to `/ROBCO/BOOT.CFG` in place of the one-entry config |

The agent's menu finds the two images through `[entry MAINT]` and `[entry DIAG]` in `BOOT.CFG`. RETROS-BIOS's `boot/BOOT.CFG` has both entries, so the nightly run passes it as `BOOT_CFG`. The card gets no `[maint]` section unless the given file has one. Without it, the maintenance image keeps restore users off.

## A real card

```sh
make bootdir DIR=path/to/dir RETROS_IMG=path/to/RETROS.IMG MFBOOT_SYS=path/to/MFBOOT.SYS
```

This runs `make sdimage CHAIN=1` and copies its staged files into `DIR`, plus the Pi firmware files `bootcode.bin`, `start.elf` and `fixup.dat` from RobCo-Core's `tools/fetch_firmware.sh`. `DIR` gets `RETROS.IMG`, its `config.txt`, the `ROBCO` folder with `MFBOOT.SYS`, `UOS.SYS` and `BOOT.CFG`, and the `UOS` tree from `card/`. `MAINT_SYS`, `DIAG_SYS` and `BOOT_CFG` work as for the chain image. Without `RETROS_IMG` or `MFBOOT_SYS` it stops: UOS alone cannot start on a real board.

Copy the files to the root of a FAT32 card. The target writes a folder. It never formats a card and never writes a device. `FIRMWARE=0` leaves the firmware out, for a card that already has it and for the tests. No card from this target has booted a board yet.

| Variable | Effect |
|---|---|
| `DIR=<dir>` | The folder to write. Required |
| `FIRMWARE=0` | Leaves the firmware files out and needs no network |
| `CLEAN=1` | Removes files an earlier run wrote that this run does not |

### Running bootdir again

Use a new, empty folder and it is one command. `bootdir` writes `UOSBOOT.LST` in `DIR`, the list of files it wrote. The card does not need it, but it does no harm there. A later run into the same folder checks the folder against the list before it changes anything:

- A file no run wrote stops the run, and the run names it. `bootdir` never removes or replaces such a file.
- A file an earlier run wrote that this run does not, such as `ROBCO/MAINT.SYS` after a run without `MAINT_SYS`, stops the run. `CLEAN=1` removes it.
- Every other file is replaced, even one you edited, such as `ROBCO/BOOT.CFG` or `UOS/UOS.CFG`. Keep your edits outside `DIR`, or pass them as `BOOT_CFG`.

Pip-OS keeps edited settings files across runs with a helper of its own. RobCo-Core has no such helper, so UOS takes this smaller rule.

## Where module sources go

The Makefile finds sources by wildcard. A module branch only adds files:

- Every `src/<dir>/*.c` builds into the image.
- The pure files the design names are picked up when they exist: `src/dcl/*.c`, `src/dev/devcfg.c`, `src/dev/devstate.c`, `src/hack/hack.c`, `src/holo/holofmt.c`, `src/holo/holoread.c`, `src/osk/osk.c`, `src/session/lockout.c`, `src/session/sha256.c`, `src/session/uaf.c`, `src/tui/pager.c`, `src/tui/tui.c`, `src/tui/tuicfg.c`, `src/vfs/path.c`. Another pure file needs one line in `PURE_NAMED` or `PURE_KERNEL`.
- Every `tests/host/test_*.c` is a host test. It links all pure files and the library's `src/lib/`.
- Every `src/<dir>/` is an include directory for the image and the host tests.
- A generated header lands in `build/host/gen/` before any host test builds: `uaf_vectors.h`, from `tests/vectors/uaf.json`.
- `make test-tools` builds the host tools the tool tests call, such as `build/host/holocheck`, then runs pytest over `tests/tools/`.
