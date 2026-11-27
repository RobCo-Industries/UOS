# UOS, the third stage of the terminal chain. Built with the RobCo-Core
# library in external/robco-core.
#
#   make                      build/UOS.SYS, the sealed image
#   make test                 host tests, then target tests on every board
#   make test BOARD=raspi0    the same, target tests on one board
#   make test-host            host unit tests of the pure files and the pin check
#   make test-target          QEMU target tests (BOARD=all by default).
#                             MFBOOT_SYS=<MFBOOT.SYS> RETROS_IMG=<RETROS.IMG>
#                             add the whole-chain tests
#   make test-tools           pytest over tests/tools/
#   make sdimage              build/sd.img: UOS.SYS and the card/ tree.
#                             CHAIN=1 RETROS_IMG=... MFBOOT_SYS=... adds the
#                             first two stages and their config.
#                             MAINT_SYS=... DIAG_SYS=... add the boot agent's
#                             maintenance and diagnostics images, and
#                             BOOT_CFG=<file> replaces the chain's BOOT.CFG
#   make run BOARD=raspi2b    boot build/sd.img in QEMU on this terminal
#                             (WINDOW=1 also opens the display window)
#   make check-pins           CI workflow pins and doc links match the submodule,
#                             then check-workflows
#   make check-workflows      no ${{ }} in a run: block and every action pinned
#                             by commit in .github/workflows/
#   make sfx-table            src/sfx/sfx_table.c and .h from src/sfx/sfx_models.json
#   make sfx-table-check      the committed table matches the model; every image
#                             build runs it once per change of its inputs
#   make sfx-preview          build/sfx_preview.wav: a session's sounds, rendered
#                             from a QEMU log (BOARD=raspi2b by default)
#   make ci-local             the CI build job in the robco-ci container
#                             (CI_LOCAL_CPUS=4 by default)
#   make clean

ROBCO_CORE := external/robco-core
IMAGE      := uos
LINK_ADDR  := 0x00400000
CONFIG_H   := src/kernel/robco_config.h
BUILD      ?= build
# One spelling per build folder. gcc -MMD names the object in each .d
# file as the compile rule spelled it, and make matches that name as a
# string. A test fixture passes an absolute BUILD while `make all` uses
# build: the .d of one never matched the other's target, so a header
# change did not rebuild the object. Strip this folder from the front.
override BUILD := $(patsubst $(CURDIR)/%,%,$(BUILD))
PYTHON     ?= python3

# Every src/<dir>/*.c builds into the image.
SRCS       := $(sort $(wildcard src/*/*.c src/*/*.S))

# Pure files: no hardware access. The host tests link all of them. Two
# lists: the pure files the design names, picked up when they exist, and
# the kernel's own pure files, one per line and sorted.
PURE_NAMED := src/console/conview.c \
              src/console/lineedit.c \
              src/dcl/*.c \
              src/dev/devcfg.c src/dev/devsplan.c src/dev/devstate.c \
              src/hack/hack.c src/hack/hackui.c \
              src/holo/holofmt.c src/holo/holoread.c src/holo/holoscan.c \
              src/osk/osk.c \
              src/session/lockout.c src/session/login.c src/session/sha256.c \
              src/session/uaf.c \
              src/sfx/sfx_table.c src/sfx/sfxrules.c \
              src/shell/shrules.c \
              src/tui/pager.c src/tui/tui.c src/tui/tuicfg.c \
              src/vfs/path.c \
              src/vfs/vfsrules.c
PURE_KERNEL := \
              src/kernel/bootlines.c \
              src/kernel/buttons.c \
              src/kernel/event.c \
              src/kernel/hwdog.c \
              src/kernel/maintreg.c \
              src/kernel/portcall.c \
              src/kernel/screen.c \
              src/kernel/service.c \
              src/kernel/settings.c \
              src/kernel/text.c \
              src/kernel/timerq.c \
              src/kernel/u64dec.c \


PURE_SRCS  := $(sort $(wildcard $(PURE_NAMED))) $(PURE_KERNEL)

# Host unit tests: every tests/host/test_*.c links every pure file. Each
# source folder is an include directory, so a test includes "uos_timer.h"
# or "kernel/uos_timer.h" alike. Files in tests/host/ not named test_*.c
# are host tools, built by test-tools.
HOST_TEST_DIR := tests/host
HOST_SRCS     := $(PURE_SRCS)
SRC_INCLUDES  := src $(patsubst %/,%,$(sort $(dir $(wildcard src/*/*.c src/*/*.h))))

# Headers the host tests need that a tool generates: the session
# module's hash vectors, from tests/vectors/uaf.json.
HOST_GEN      := $(BUILD)/host/gen
HOST_GEN_HDRS := $(HOST_GEN)/uaf_vectors.h
HOST_INCLUDES := $(SRC_INCLUDES) $(HOST_GEN)
# Tests run from the repository root and read the shipped card files.
HOST_DEFINES  := UAF_CARD_PATH='"card/UOS/SYSUAF.DAT"'
# The library's FatFs glue over a host file: test_vfs_card.c runs vfs.c on it.
HOST_CORE_SETS := fs fatfs

ifeq ($(wildcard $(ROBCO_CORE)/mk/robco.mk),)
$(error $(ROBCO_CORE) is empty: run git submodule update --init)
endif

# `make sdimage`. Set before the include: robco.mk reads them.
SDIMAGE_MIB   ?= 64
SDIMAGE_DEPS  := $(BUILD)/UOS.SYS
SDIMAGE_DIR   := card
SDIMAGE_FILES := $(BUILD)/UOS.SYS:ROBCO/UOS.SYS \
                 $(if $(filter 1,$(CHAIN)),$(MFBOOT_SYS):ROBCO/MFBOOT.SYS) \
                 $(if $(MAINT_SYS),$(MAINT_SYS):ROBCO/MAINT.SYS) \
                 $(if $(DIAG_SYS),$(DIAG_SYS):ROBCO/DIAG.SYS)

.DEFAULT_GOAL := all
include $(ROBCO_CORE)/mk/robco.mk
include $(ROBCO_CORE)/mk/host.mk

# The image sees the source folders as include directories.
ROBCO_CFLAGS += $(addprefix -I,$(SRC_INCLUDES))

# Target tests boot the images built here; keep top-level targets serial.
.NOTPARALLEL:

# The card name of the image. A stale sound table stops the build: see
# the stamp below, which every path to the image goes through.
all: $(BUILD)/UOS.SYS

# ---- Sound tables -----------------------------------------------------

# The library's generator writes the tables from the model file. Both
# outputs are committed. The check compares the model's hash and the
# generator's revision with the header: it is fast. The tool tests run
# `--check --full`, which renders again and compares bytes.
SFX_TABLE      := $(ROBCO_CORE)/tools/sfx_table.py
SFX_TABLE_ARGS := src/sfx/sfx_models.json --c src/sfx/sfx_table.c --h src/sfx/sfx_table.h \
                  --prefix uos_sfx_ --bed fan

.PHONY: sfx-table sfx-table-check
sfx-table:
	$(PYTHON) $(SFX_TABLE) $(SFX_TABLE_ARGS)

sfx-table-check:
	$(PYTHON) $(SFX_TABLE) $(SFX_TABLE_ARGS) --check

# The objects that read the tables wait for a passed check, so `make`,
# `make sdimage` and `make run` all stop on a stale table. The stamp
# runs the check again only when one of its inputs changes.
SFX_STAMP := $(BUILD)/sfx-table.ok
$(SFX_STAMP): src/sfx/sfx_models.json src/sfx/sfx_table.c src/sfx/sfx_table.h $(SFX_TABLE)
	$(PYTHON) $(SFX_TABLE) $(SFX_TABLE_ARGS) --check
	@mkdir -p $(@D)
	@touch $@

$(BUILD)/obj/src/sfx/sfx_table.o $(BUILD)/obj/src/sfx/sfx.o: $(SFX_STAMP)

$(BUILD)/UOS.SYS: $(BUILD)/$(IMAGE).img
	cp $< $@

# ---- Python environment for the target tests --------------------------

VENV := .venv

.PHONY: venv
venv: $(VENV)/.installed

$(VENV)/.installed: $(ROBCO_CORE)/tests/requirements.txt
	$(PYTHON) -m venv $(VENV)
	$(VENV)/bin/pip install -q -r $<
	touch $@

# ---- Tests ------------------------------------------------------------

BOARD ?= all

.PHONY: test test-target test-tools check-pins check-doc-links
test: test-host
	$(MAKE) test-target

test-target: all venv
	$(VENV)/bin/pytest tests -v --ignore=tests/tools --ignore=tests/host \
	    --board $(BOARD) --build-dir $(BUILD) \
	    $(if $(RETROS_IMG),--retros-img $(abspath $(RETROS_IMG))) \
	    $(if $(MFBOOT_SYS),--mfboot-sys $(abspath $(MFBOOT_SYS)))

# Host tools the tool tests call. holocheck validates tapes with the C
# reader, so tests/tools/test_mkholo.py checks both sides agree.
HOST_TOOLS := $(HOST_BUILD)/holocheck

$(HOST_BUILD)/holocheck: tests/host/holocheck.c $(HOST_LINK) $(HOST_HDRS)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< $(HOST_LINK)

test-tools: venv $(HOST_TOOLS)
	ROBCO_CORE=$(abspath $(ROBCO_CORE)) HOLO_CHECK=$(abspath $(HOST_BUILD)/holocheck) \
	    $(VENV)/bin/pytest tests/tools -q -p no:cacheprovider

# DOC_ROOT: the tree whose Markdown links check-doc-links reads.
DOC_ROOT ?= .

check-pins: check-doc-links check-workflows
	$(ROBCO_CORE)/tools/check_pins.sh .github/workflows/ci.yml

# check-workflows comes from the library's mk/robco.mk: no ${{ }} inside a
# run: block, and every action or reusable workflow pinned by a full commit.

check-doc-links:
	$(ROBCO_CORE)/tools/check_doc_links.sh $(DOC_ROOT) external/robco-core

# Host unit tests come from mk/host.mk. The pin check runs first. The
# library's test-host-gccwarn compiles every host C file with the cross
# GCC in two passes: CI's flags with the sanitizers, and -O2.
test-host: check-pins

# `make ci-local` and `make ci-local-image` come from mk/host.mk. They run
# the CI build job's targets in the robco-ci container.
CI_LOCAL_TARGETS := all test-host

# Generated headers exist before any host test builds.
$(HOST_TESTS): $(HOST_GEN_HDRS)
# test_vfs_card.c includes vfs.c: rebuild it when vfs.c changes.
$(HOST_BUILD)/test_vfs_card: src/vfs/vfs.c

$(HOST_GEN)/uaf_vectors.h: tests/vectors/uaf.json tools/mkuaf.py
	@mkdir -p $(dir $@)
	$(PYTHON) tools/mkuaf.py vectors-h $< -o $@

# ---- Cards and QEMU -----------------------------------------------------

# CHAIN=1 puts RETROS-BIOS on the card as the first stage, with its
# config.txt, MFBootAgent as /ROBCO/MFBOOT.SYS and a BOOT.CFG that boots
# UOS, so the card boots the whole chain. BOOT_CFG=<file> gives that
# BOOT.CFG instead, such as RETROS-BIOS's boot/BOOT.CFG with the MAINT and
# DIAG entries. MAINT_SYS and DIAG_SYS go to /ROBCO/MAINT.SYS and
# /ROBCO/DIAG.SYS, the paths of those entries.
CHAIN_CFG := [boot]\ndefault = UOS\ntimeout = 3\n\n[entry UOS]\npath = /ROBCO/UOS.SYS\n

sdimage-stage::
ifeq ($(CHAIN),1)
	$(if $(RETROS_IMG),,$(error CHAIN=1 needs RETROS_IMG=<RETROS.IMG>))
	$(if $(MFBOOT_SYS),,$(error CHAIN=1 needs MFBOOT_SYS=<MFBOOT.SYS>))
	$(ROBCO_CORE)/tools/mkbootdir.sh $(RETROS_IMG) $(SDIMAGE_STAGE) \
	    --kernel-name RETROS.IMG --no-firmware
ifneq ($(BOOT_CFG),)
	cp $(BOOT_CFG) $(SDIMAGE_STAGE)/ROBCO/BOOT.CFG
else
	printf '$(CHAIN_CFG)' > $(SDIMAGE_STAGE)/ROBCO/BOOT.CFG
endif
endif

# Alone, a two-word image at 0x8000 jumps to UOS at its link address.
$(BUILD)/tramp.img:
	@mkdir -p $(dir $@)
	$(PYTHON) -c "import struct, sys; open(sys.argv[1], 'wb').write(struct.pack('<II', 0xE51FF004, $(LINK_ADDR)))" $@

.PHONY: run
comma := ,
RUN_ALONE := $(BUILD)/tramp.img --qemu-arg=-device \
             --qemu-arg=loader$(comma)file=$(BUILD)/$(IMAGE).img$(comma)addr=$(LINK_ADDR)$(comma)force-raw=on
RUN_BOARD := $(if $(filter all,$(BOARD)),raspi2b,$(BOARD))
run: sdimage $(BUILD)/tramp.img
	PYTHONPATH=$(ROBCO_CORE)/tests $(PYTHON) -m harness.run \
	    $(if $(filter 1,$(CHAIN)),$(RETROS_IMG),$(RUN_ALONE)) \
	    --board $(RUN_BOARD) --sd $(BUILD)/sd.img --serial2 pty $(if $(filter 1,$(WINDOW)),--window)

# QEMU has no sound. `make sfx-preview` boots a card on one board, raspi2b
# unless BOARD names one, with [audio] sound = on and trace = on, and
# drives a short session through the maintenance port. It saves the
# serial log, dumps the sound tables as .u8 files and renders
# build/sfx_preview.wav with RobCo-Core's tools/audio_preview.py. It stops
# when the log is missing, when the tool prints any warning, such as a
# buffer it cannot find, or when the WAV is silent. See docs/TESTING.md.
SFX_PREVIEW_BOARD := $(if $(filter all,$(BOARD)),raspi2b,$(BOARD))
SFX_PREVIEW_DIR   := $(BUILD)/sfx_preview
SFX_PREVIEW_WAV   := $(BUILD)/sfx_preview.wav

.PHONY: sfx-preview
sfx-preview: venv
	@rm -rf $(SFX_PREVIEW_DIR) $(SFX_PREVIEW_WAV)
	@mkdir -p $(SFX_PREVIEW_DIR)
	$(VENV)/bin/pytest tests/test_sfx_preview_log.py -q --tb=short -p no:cacheprovider \
	    --board $(SFX_PREVIEW_BOARD) --build-dir $(BUILD) \
	    --sfx-preview-log $(abspath $(SFX_PREVIEW_DIR))/serial.log
	@test -s $(SFX_PREVIEW_DIR)/serial.log || { \
	    echo "sfx-preview: the session did not run, so there is no log" >&2; exit 1; }
	$(PYTHON) -I $(SFX_TABLE) src/sfx/sfx_models.json --dump $(SFX_PREVIEW_DIR)/pcm --bed fan
	$(PYTHON) -I $(ROBCO_CORE)/tools/audio_preview.py $(SFX_PREVIEW_DIR)/serial.log \
	    $(SFX_PREVIEW_WAV) --pcm $(SFX_PREVIEW_DIR)/pcm 2> $(SFX_PREVIEW_DIR)/preview.err
	@cat $(SFX_PREVIEW_DIR)/preview.err >&2
	@if grep -q 'warning:' $(SFX_PREVIEW_DIR)/preview.err; then \
	    echo "sfx-preview: the preview tool warned, so the WAV is wrong" >&2; exit 1; fi
	$(PYTHON) -I -c 'import sys, wave, array; w = wave.open(sys.argv[1]); \
	    a = array.array("h", w.readframes(w.getnframes())); \
	    sys.exit(0 if any(a) else "sfx-preview: the WAV is silent")' $(SFX_PREVIEW_WAV)
