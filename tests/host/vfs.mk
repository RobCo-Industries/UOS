# Standalone host tests for src/vfs/path.c.
#   make -f tests/host/vfs.mk ROBCO_CORE=/path/to/RobCo-Core
ROBCO_CORE ?= ../RobCo-Core
HOST_CC    ?= cc
CROSS_CC   ?= arm-none-eabi-gcc
B          := build/host-vfs

HOST_CFLAGS  := -std=gnu11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
                -DROBCO_HOST -I$(ROBCO_CORE)/include -I$(ROBCO_CORE)/tests/host -Isrc/vfs
CROSS_CFLAGS := -mcpu=arm1176jzf-s -marm -mfloat-abi=soft -mgeneral-regs-only \
                -mno-unaligned-access -std=gnu11 -ffreestanding -Wall -Wextra -Werror \
                -I$(ROBCO_CORE)/include

VFS_SRCS  := $(wildcard src/vfs/path.c src/vfs/path_*.c)
LIB_SRCS  := $(ROBCO_CORE)/src/lib/k_string.c
TESTS     := $(patsubst tests/host/%.c,$(B)/%,$(wildcard tests/host/test_vfs*.c))

.PHONY: all cross run
all: cross run

cross:
	@for f in $(VFS_SRCS); do \
	    $(CROSS_CC) $(CROSS_CFLAGS) -c -o /dev/null $$f || exit 1; \
	done
	@echo "cross: $(VFS_SRCS) ok"

$(B)/%: tests/host/%.c $(VFS_SRCS) src/vfs/path.h
	@mkdir -p $(B)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< $(VFS_SRCS) $(LIB_SRCS)

run: $(TESTS)
	@for t in $(TESTS); do $$t > $$t.log || { cat $$t.log; exit 1; }; tail -1 $$t.log; done
