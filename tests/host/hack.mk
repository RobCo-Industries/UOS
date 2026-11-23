# Standalone host tests for src/hack, without the skeleton:
#   make -f tests/host/hack.mk ROBCO_CORE=/path/to/RobCo-Core
# Builds and runs the host tests with sanitizers, then compiles every
# module source with the cross GCC and the target flags.

ROBCO_CORE ?= ../RobCo-Core
HOST_CC    ?= cc
CROSS_CC   ?= arm-none-eabi-gcc
PYTHON     ?= python3
BUILD_DIR  ?= build/host-hack

HK_SRCS   := src/hack/hack.c
HK_TESTS  := test_hack
LIB_SRCS  := $(ROBCO_CORE)/src/lib/k_string.c $(ROBCO_CORE)/src/lib/k_printf.c

HOST_FLAGS := -std=gnu11 -Wall -Wextra -Werror -g -O1 -DROBCO_HOST=1 \
              -fsanitize=address,undefined -fno-sanitize-recover=undefined \
              -fno-omit-frame-pointer \
              -I $(ROBCO_CORE)/include -I $(ROBCO_CORE)/tests/host -I src/hack
CROSS_FLAGS := -mcpu=arm1176jzf-s -marm -mfloat-abi=soft -mgeneral-regs-only \
               -mno-unaligned-access -std=gnu11 -ffreestanding -Wall -Wextra -Werror \
               -O2 -I $(ROBCO_CORE)/include

.PHONY: all cross run tools
all: cross run tools

$(BUILD_DIR)/%: tests/host/%.c $(HK_SRCS) src/hack/hack.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_FLAGS) -o $@ $< $(HK_SRCS) $(LIB_SRCS)

cross:
	@for f in $(HK_SRCS); do $(CROSS_CC) $(CROSS_FLAGS) -c -o /dev/null $$f || exit 1; done
	@echo "hack.mk: $(CROSS_CC) built $(HK_SRCS) with no warning"

run: $(addprefix $(BUILD_DIR)/,$(HK_TESTS))
	@for t in $^; do $$t || exit 1; done

tools:
	$(PYTHON) tests/tools/test_mkwords.py
