# Standalone host tests for src/dcl. Run from the repository root:
#   make -f tests/host/dcl.mk ROBCO_CORE=/path/to/RobCo-Core

ROBCO_CORE ?= ../RobCo-Core
HOST_CC    ?= cc
CROSS_CC   ?= arm-none-eabi-gcc
BUILD_DIR  ?= build/host-dcl

DCL_SRCS  := src/dcl/dcl.c src/dcl/msg.c src/dcl/tables.c
LIB_SRCS  := $(ROBCO_CORE)/src/lib/k_string.c $(ROBCO_CORE)/src/lib/k_printf.c
TESTS     := test_dcl test_dcl_msg test_dcl_table

CFLAGS := -std=gnu11 -Wall -Wextra -Werror -g -O1 -DROBCO_HOST=1 \
          -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer \
          -I $(ROBCO_CORE)/include -I $(ROBCO_CORE)/tests/host -I src/dcl
CROSS_FLAGS := -mcpu=arm1176jzf-s -marm -mfloat-abi=soft -mgeneral-regs-only -mno-unaligned-access \
               -std=gnu11 -ffreestanding -Wall -Wextra -Werror -O2 -I $(ROBCO_CORE)/include -I src/dcl

.PHONY: all run cross gccwarn
all: run

$(BUILD_DIR)/%: tests/host/%.c $(DCL_SRCS) src/dcl/dcl.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(CFLAGS) -o $@ $< $(DCL_SRCS) $(LIB_SRCS)

cross:
	@for f in $(DCL_SRCS); do $(CROSS_CC) $(CROSS_FLAGS) -c -o /dev/null $$f || exit 1; done
	@echo "dcl: cross build clean"

# GCC's warnings on the tests too, with the host headers (skipped without xcrun).
gccwarn:
	@inc="$$(xcrun --show-sdk-path 2>/dev/null)/usr/include"; \
	if [ ! -f "$$inc/stdio.h" ]; then echo "dcl: gccwarn skipped, no host headers"; exit 0; fi; \
	for f in tests/host/test_dcl*.c; do \
	  $(CROSS_CC) -c -o /dev/null -std=gnu11 -Wall -Wextra -Werror -O2 -DROBCO_HOST=1 \
	    -I $(ROBCO_CORE)/include -I $(ROBCO_CORE)/tests/host -I src/dcl \
	    -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -idirafter "$$inc" $$f || exit 1; \
	done; echo "dcl: gccwarn clean"

run: cross gccwarn $(addprefix $(BUILD_DIR)/,$(TESTS))
	@failed=""; for t in $(TESTS); do $(BUILD_DIR)/$$t || failed="$$failed $$t"; done; \
	if [ -n "$$failed" ]; then echo "dcl: FAILED:$$failed" >&2; exit 1; fi; echo "dcl: all passed"
