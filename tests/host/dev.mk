# Standalone host tests for src/dev.
#   make -f tests/host/dev.mk ROBCO_CORE=/path/to/RobCo-Core

ROBCO_CORE ?= ../RobCo-Core
HOST_CC    ?= cc
CROSS_CC   ?= arm-none-eabi-gcc
BUILD_DIR  := build/host-dev

CFLAGS := -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
          -I$(ROBCO_CORE)/include -I$(ROBCO_CORE)/tests/host -Isrc/dev
CROSS_FLAGS := -mcpu=arm1176jzf-s -marm -mfloat-abi=soft -mgeneral-regs-only \
               -mno-unaligned-access -std=gnu11 -ffreestanding -Wall -Wextra -Werror \
               -I$(ROBCO_CORE)/include

DEV_SRCS := src/dev/devcfg.c src/dev/devstate.c
LIB_SRCS := $(ROBCO_CORE)/src/lib/ini.c $(ROBCO_CORE)/src/lib/k_string.c \
            $(ROBCO_CORE)/src/lib/k_printf.c
TESTS    := $(BUILD_DIR)/test_dev

.PHONY: all cross run
all: cross run

cross: $(DEV_SRCS) src/dev/dev.h
	@for f in $(DEV_SRCS); do \
	    $(CROSS_CC) $(CROSS_FLAGS) -c -o /dev/null $$f || exit 1; \
	done
	@echo "cross: ok"

$(BUILD_DIR)/test_dev: tests/host/test_dev.c $(DEV_SRCS) src/dev/dev.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(CFLAGS) -o $@ tests/host/test_dev.c $(DEV_SRCS) $(LIB_SRCS)

run: $(TESTS)
	@for t in $(TESTS); do $$t > $$t.log 2>&1; rc=$$?; grep -E 'FAIL|tests,' $$t.log; \
	    grep -A3 -E '^  ' $$t.log | head -40; [ $$rc -eq 0 ] || exit 1; done
