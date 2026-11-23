# Standalone host tests for src/osk.
#   make -f tests/host/osk.mk ROBCO_CORE=/path/to/RobCo-Core

ROBCO_CORE ?= ../RobCo-Core
HOST_CC    ?= cc
CROSS_CC   ?= arm-none-eabi-gcc
BUILD_DIR  := build/host-osk

SRCS      := src/osk/osk.c
LIB_SRCS  := $(ROBCO_CORE)/src/lib/k_string.c
CFLAGS    := -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
             -I$(ROBCO_CORE)/include -I$(ROBCO_CORE)/tests/host -Isrc/osk
CROSS_FLAGS := -mcpu=arm1176jzf-s -marm -mfloat-abi=soft -mgeneral-regs-only \
               -mno-unaligned-access -std=gnu11 -ffreestanding -Wall -Wextra -Werror \
               -I$(ROBCO_CORE)/include -Isrc/osk

.PHONY: all test cross
all: test cross

$(BUILD_DIR)/test_osk: tests/host/test_osk.c $(SRCS) src/osk/osk.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(CFLAGS) -o $@ tests/host/test_osk.c $(SRCS) $(LIB_SRCS)

test: $(BUILD_DIR)/test_osk
	$(BUILD_DIR)/test_osk

cross:
	$(foreach s,$(SRCS),$(CROSS_CC) $(CROSS_FLAGS) -c -o /dev/null $(s) &&) true
