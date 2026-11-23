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
#                             first two stages and their config
#   make run BOARD=raspi2b    boot build/sd.img in QEMU on this terminal
#                             (WINDOW=1 also opens the display window)
#   make check-pins           CI workflow pins and doc links match the submodule
#   make clean

ROBCO_CORE := external/robco-core
IMAGE      := uos
LINK_ADDR  := 0x00400000
CONFIG_H   := src/kernel/robco_config.h
BUILD      ?= build
PYTHON     ?= python3

# Every src/<dir>/*.c builds into the image.
SRCS       := $(sort $(wildcard src/*/*.c src/*/*.S))

# Pure files: no hardware access. The host tests link all of them. Two
# lists: the pure files the design names, picked up when they exist, and
# the kernel's own pure files, one per line and sorted.
PURE_NAMED := src/console/conview.c \
              src/console/lineedit.c \
              src/dcl/*.c \
              src/dev/devcfg.c src/dev/devstate.c \
              src/hack/hack.c \
              src/holo/holofmt.c src/holo/holoread.c \
              src/osk/osk.c \
              src/session/lockout.c src/session/sha256.c src/session/uaf.c \
              src/tui/pager.c src/tui/tui.c src/tui/tuicfg.c \
              src/vfs/path.c \
              src/vfs/vfsrules.c
PURE_KERNEL := \
              src/kernel/bootlines.c \
              src/kernel/event.c \
              src/kernel/maintreg.c \
              src/kernel/screen.c \
              src/kernel/service.c \
              src/kernel/settings.c \
              src/kernel/timerq.c \


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

ifeq ($(wildcard $(ROBCO_CORE)/mk/robco.mk),)
$(error $(ROBCO_CORE) is empty: run git submodule update --init)
endif

# `make sdimage`. Set before the include: robco.mk reads them.
SDIMAGE_MIB   ?= 64
SDIMAGE_DEPS  := $(BUILD)/UOS.SYS
SDIMAGE_DIR   := card
SDIMAGE_FILES := $(BUILD)/UOS.SYS:ROBCO/UOS.SYS \
                 $(if $(filter 1,$(CHAIN)),$(MFBOOT_SYS):ROBCO/MFBOOT.SYS)

.DEFAULT_GOAL := all
include $(ROBCO_CORE)/mk/robco.mk
include $(ROBCO_CORE)/mk/host.mk

# The image sees the source folders as include directories.
ROBCO_CFLAGS += $(addprefix -I,$(SRC_INCLUDES))

# Target tests boot the images built here; keep top-level targets serial.
.NOTPARALLEL:

# The card name of the image.
all: $(BUILD)/UOS.SYS

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

check-pins: check-doc-links
	$(ROBCO_CORE)/tools/check_pins.sh .github/workflows/ci.yml

check-doc-links:
	$(ROBCO_CORE)/tools/check_doc_links.sh $(DOC_ROOT) external/robco-core

# Host unit tests come from mk/host.mk. The pin check runs first.
test-host: check-pins

# Generated headers exist before any host test builds.
$(HOST_TESTS): $(HOST_GEN_HDRS)

$(HOST_GEN)/uaf_vectors.h: tests/vectors/uaf.json tools/mkuaf.py
	@mkdir -p $(dir $@)
	$(PYTHON) tools/mkuaf.py vectors-h $< -o $@

# ---- Cards and QEMU -----------------------------------------------------

# CHAIN=1 puts RETROS-BIOS on the card as the first stage, with its
# config.txt, MFBootAgent as /ROBCO/MFBOOT.SYS and a BOOT.CFG that boots
# UOS, so the card boots the whole chain.
CHAIN_CFG := [boot]\ndefault = UOS\ntimeout = 3\n\n[entry UOS]\npath = /ROBCO/UOS.SYS\n

sdimage-stage::
ifeq ($(CHAIN),1)
	$(if $(RETROS_IMG),,$(error CHAIN=1 needs RETROS_IMG=<RETROS.IMG>))
	$(if $(MFBOOT_SYS),,$(error CHAIN=1 needs MFBOOT_SYS=<MFBOOT.SYS>))
	$(ROBCO_CORE)/tools/mkbootdir.sh $(RETROS_IMG) $(SDIMAGE_STAGE) \
	    --kernel-name RETROS.IMG --no-firmware
	printf '$(CHAIN_CFG)' > $(SDIMAGE_STAGE)/ROBCO/BOOT.CFG
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
