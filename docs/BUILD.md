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
| `DIR=<dir>` | The folder to write. Required. It may hold spaces: `DIR='/Volumes/UOS CARD'`. It cannot be, be inside or hold `build/sdstage` or `build/bootstage`, the folders the build clears: the target stops first and names it |
| `FIRMWARE=0` | Leaves the firmware files out and needs no network |

### Running bootdir again

Use a new, empty folder for the first run. A first run stops when `DIR` holds files and no `UOSBOOT.LST`, and changes nothing. `UOSBOOT.LST` has one line per file a run wrote: the SHA-256 of the bytes written, two spaces and the path, as `shasum -a 256` prints it. The card does not need it, but it does no harm there.

### Updating a card in use

You can run `make bootdir` again with `DIR` set to the card itself, or to a copy of it. The run checks everything before it writes anything, so a run that stops on a check has changed nothing.

It writes each file, and `UOSBOOT.LST` last, to `UOSBOOT.TMP` in the same folder first, then moves it into place. A copy that fails, such as on a full card or a card pulled out, leaves the old file as it was. A run that stopped half way has written some files and not the new list. Run it again: it removes every `UOSBOOT.TMP` it finds, takes a file whose bytes already match the build as written, says nothing about it, and finishes the rest. Do not keep your own files named `UOSBOOT.TMP` in `DIR`.

- It replaces a file it wrote last time when nobody changed it since, such as `ROBCO/UOS.SYS`.
- It keeps a file it wrote that has changed since, such as `UOS/SYSUAF.DAT` after a password change or an edited `UOS/UOS.CFG`, and prints a line that names it. The next run keeps it again. `REPLACE=1` writes the shipped file over it.
- It leaves alone every file no run wrote that it does not ship, such as a file a user saved in a home folder. It says nothing about them.
- It stops when a file it would write is already there and no run wrote it, and names the file.
- It stops when a file or folder on its way is a symbolic link, and names it. It never follows a link inside `DIR` and never writes or removes through one. `DIR` itself may be a link.
- It stops when a file sits where it needs a folder, or a folder sits where it writes a file, and names it.
- It stops when a file it wrote last time is no longer part of the card and nobody changed it, such as `ROBCO/MAINT.SYS` after a run without `MAINT_SYS`. `CLEAN=1` removes that file.
- A changed file never stops a run, whether the card still ships it or not. The run keeps it, names it and keeps its line in `UOSBOOT.LST`, so a later run that ships it again treats it as a changed file: kept, or replaced with `REPLACE=1`.
- It never deletes or moves a folder, so home folders stay.
- It compares names without regard to case, as a FAT32 card does: `FOO.TXT` from the build and `foo.txt` on the card are one file. It writes and names a file as the build spells it, through the entry already on the card, so no second entry appears. A folder keeps its spelling on the card. It stops when two names in `DIR` differ only by case, which a FAT32 card cannot hold, and names them.
- With `FIRMWARE=0` it leaves the firmware files alone. It does not stop on them, `CLEAN=1` does not remove them, and their lines stay in `UOSBOOT.LST`, so a later run with the firmware still knows them.

| Variable | Effect |
|---|---|
| `CLEAN=1` | Removes unchanged files an earlier run wrote that this run does not |
| `REPLACE=1` | Also replaces files changed since the last run. Never touches a file no run wrote |

## Where module sources go

The Makefile finds sources by wildcard. A module branch only adds files:

- Every `src/<dir>/*.c` builds into the image.
- The pure files the design names are picked up when they exist: `src/dcl/*.c`, `src/dev/devcfg.c`, `src/dev/devstate.c`, `src/hack/hack.c`, `src/holo/holofmt.c`, `src/holo/holoread.c`, `src/osk/osk.c`, `src/session/lockout.c`, `src/session/sha256.c`, `src/session/uaf.c`, `src/tui/pager.c`, `src/tui/tui.c`, `src/tui/tuicfg.c`, `src/vfs/path.c`. Another pure file needs one line in `PURE_NAMED` or `PURE_KERNEL`.
- Every `tests/host/test_*.c` is a host test. It links all pure files and the library's `src/lib/`.
- Every `src/<dir>/` is an include directory for the image and the host tests.
- A generated header lands in `build/host/gen/` before any host test builds: `uaf_vectors.h`, from `tests/vectors/uaf.json`.
- `make test-tools` builds the host tools the tool tests call, such as `build/host/holocheck`, then runs pytest over `tests/tools/`.
