# Standalone host tests of the tui module.
#   make -f tests/host/tui.mk ROBCO_CORE=/path/to/RobCo-Core
# Builds under build/host-tui, runs every test, then compiles every
# module source with the cross compiler and the target flags.

ROBCO_CORE ?= ../RobCo-Core
HOST_CC    ?= cc
CROSS_CC   ?= arm-none-eabi-gcc
OUT        := build/host-tui

CFLAGS := -std=gnu11 -Wall -Wextra -Werror -g -O1 -DROBCO_HOST=1 \
          -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer \
          -I $(ROBCO_CORE)/include -I $(ROBCO_CORE)/tests/host -I src/tui

TARGET_FLAGS := -mcpu=arm1176jzf-s -marm -mfloat-abi=soft -mgeneral-regs-only \
                -mno-unaligned-access -std=gnu11 -ffreestanding -Wall -Wextra -Werror \
                -I $(ROBCO_CORE)/include

LIB_SRCS := $(ROBCO_CORE)/src/lib/k_string.c $(ROBCO_CORE)/src/lib/k_printf.c \
            $(ROBCO_CORE)/src/lib/ini.c
MOD_SRCS := src/tui/tuicfg.c src/tui/tui.c src/tui/pager.c
TESTS    := test_tui_cfg test_tui test_tui_pager

.PHONY: all run cross
all: run cross

$(OUT)/%: tests/host/%.c $(MOD_SRCS) $(LIB_SRCS) src/tui/tui.h
	@mkdir -p $(OUT)
	$(HOST_CC) $(CFLAGS) -o $@ $< $(MOD_SRCS) $(LIB_SRCS)

run: $(addprefix $(OUT)/,$(TESTS))
	@set -e; for t in $^; do ./$$t > $$t.log || { grep -v "^ok" $$t.log; exit 1; }; tail -1 $$t.log; done

cross:
	@set -e; for f in $(MOD_SRCS); do \
	    $(CROSS_CC) $(TARGET_FLAGS) -c -o /dev/null $$f; echo "cross ok $$f"; done
