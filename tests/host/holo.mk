# Standalone build and test of the holo module.
#   make -f tests/host/holo.mk ROBCO_CORE=/path/to/RobCo-Core
# Targets: test (default: host tests, cross check, tool tests), host, cross, tool.
# PYTHON must have pytest for the tool tests.

ROBCO_CORE ?= ../RobCo-Core
HOST_CC    ?= cc
CROSS_CC   ?= arm-none-eabi-gcc
PYTHON     ?= python3
OUT        := build/holo

HOLO_SRCS  := src/holo/holofmt.c src/holo/holoread.c
CORE_SRCS  := $(ROBCO_CORE)/src/lib/rimg.c $(ROBCO_CORE)/src/lib/crc32.c \
              $(ROBCO_CORE)/src/lib/k_string.c $(ROBCO_CORE)/src/lib/k_printf.c

HOST_CFLAGS := -std=gnu11 -Wall -Wextra -Werror -g -O1 -DROBCO_HOST=1 \
               -fsanitize=address,undefined -fno-sanitize-recover=undefined \
               -fno-omit-frame-pointer \
               -I $(ROBCO_CORE)/include -I $(ROBCO_CORE)/tests/host -I src

CROSS_CFLAGS := -mcpu=arm1176jzf-s -marm -mfloat-abi=soft -mgeneral-regs-only \
                -mno-unaligned-access -std=gnu11 -ffreestanding -Wall -Wextra -Werror \
                -I $(ROBCO_CORE)/include -I src

.PHONY: test host cross tool
test: host cross tool

$(OUT)/test_holo: tests/host/test_holo.c $(HOLO_SRCS) src/holo/holofmt.h
	@mkdir -p $(OUT)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ tests/host/test_holo.c $(HOLO_SRCS) $(CORE_SRCS)

$(OUT)/holocheck: tests/host/test_holo_cli.c $(HOLO_SRCS) src/holo/holofmt.h
	@mkdir -p $(OUT)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ tests/host/test_holo_cli.c $(HOLO_SRCS) $(CORE_SRCS)

host: $(OUT)/test_holo
	$(OUT)/test_holo

cross:
	@for f in $(HOLO_SRCS); do echo "cross $$f"; \
	  $(CROSS_CC) $(CROSS_CFLAGS) -c -o /dev/null $$f || exit 1; done

tool: $(OUT)/holocheck
	ROBCO_CORE=$(ROBCO_CORE) HOLO_CHECK=$(OUT)/holocheck $(PYTHON) -m pytest -q tests/tools/test_mkholo.py
