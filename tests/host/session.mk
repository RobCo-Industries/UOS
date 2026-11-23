# Standalone host tests of src/session, without the UOS skeleton.
#
#   make -f tests/host/session.mk ROBCO_CORE=/path/to/RobCo-Core
#
# Run from the repository root. Builds each tests/host/test_session_*.c with
# the host compiler and sanitizers, runs them, then compiles every module
# source with the cross compiler and the target flags. The shared hash
# vectors reach C as a header that tools/mkuaf.py generates from
# tests/vectors/uaf.json. Output goes under build/session.

ROBCO_CORE ?= ../RobCo-Core
HOST_CC    ?= cc
CROSS_CC   ?= arm-none-eabi-gcc
PYTHON     ?= python3
OUT        := build/session

SRCS   := $(sort $(wildcard src/session/*.c))
HDRS   := $(wildcard src/session/*.h)
LIB    := $(ROBCO_CORE)/src/lib/crc32.c $(ROBCO_CORE)/src/lib/k_string.c $(ROBCO_CORE)/src/lib/k_printf.c
TESTS  := $(patsubst tests/host/%.c,$(OUT)/%,$(sort $(wildcard tests/host/test_session_*.c)))
VECH   := $(if $(wildcard tests/vectors/uaf.json),$(OUT)/uaf_vectors.h)

CFLAGS := -std=gnu11 -Wall -Wextra -Werror -g -O1 -DROBCO_HOST=1 \
          -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer \
          -I $(ROBCO_CORE)/include -I $(ROBCO_CORE)/tests/host -I src/session -I $(OUT) \
          -DUAF_CARD_PATH='"card/UOS/SYSUAF.DAT"'
TFLAGS := -mcpu=arm1176jzf-s -marm -mfloat-abi=soft -mgeneral-regs-only -mno-unaligned-access \
          -std=gnu11 -ffreestanding -O2 -Wall -Wextra -Werror -I $(ROBCO_CORE)/include

.PHONY: all run cross gccwarn
all: run cross gccwarn

$(VECH): tests/vectors/uaf.json tools/mkuaf.py
	@mkdir -p $(OUT)
	$(PYTHON) tools/mkuaf.py vectors-h $< -o $@

$(OUT)/test_session_%: tests/host/test_session_%.c $(SRCS) $(HDRS) $(VECH)
	@mkdir -p $(OUT)
	$(HOST_CC) $(CFLAGS) -o $@ $< $(SRCS) $(LIB)

run: $(TESTS)
	@set -e; for t in $(TESTS); do $$t; done

cross:
	@set -e; for f in $(SRCS); do $(CROSS_CC) $(TFLAGS) -c -o /dev/null $$f; done; \
	echo "session: $(CROSS_CC) built $(words $(SRCS)) sources with no warning"

# GCC-only warnings in the tests (CI uses GCC; the host compiler may be
# clang): compile them with the cross GCC and the macOS SDK headers.
# Skips with one line when no host headers are found.
GW_FLAGS := $(filter-out -fsanitize=% -fno-sanitize-recover=% -fno-omit-frame-pointer -g -O1,$(CFLAGS)) \
            -O2 -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast
gccwarn: $(VECH)
	@inc="$(HOST_GCCWARN_INC)"; \
	if [ -z "$$inc" ] && command -v xcrun >/dev/null 2>&1; then inc="$$(xcrun --show-sdk-path 2>/dev/null)/usr/include"; fi; \
	if [ -z "$$inc" ] || [ ! -f "$$inc/stdio.h" ]; then echo "session gccwarn: skipped, no host headers"; exit 0; fi; \
	set -e; for f in $(wildcard tests/host/test_session_*.c); do \
	  $(CROSS_CC) -c -o /dev/null $(GW_FLAGS) -idirafter "$$inc" $$f; \
	done; echo "session gccwarn: no warning in the tests"
