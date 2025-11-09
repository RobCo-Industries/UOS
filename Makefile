#
# UOS Makefile
# Unified Operating System Build System
# Copyright 2075-2077 RobCo Industries
#

# Target platform (bcm2835, bcm2836, bcm2837)
PLATFORM ?= bcm2837

# Toolchain
CROSS_COMPILE ?= arm-none-eabi-
CC = $(CROSS_COMPILE)gcc
AS = $(CROSS_COMPILE)as
LD = $(CROSS_COMPILE)ld
OBJCOPY = $(CROSS_COMPILE)objcopy
OBJDUMP = $(CROSS_COMPILE)objdump

# Directories
BUILD_DIR = build
SRC_KERNEL = kernel
SRC_DRIVERS = drivers
SRC_SERVICES = services
SRC_USERSPACE = userspace
INCLUDE_DIR = include

# Platform-specific settings
ifeq ($(PLATFORM),bcm2835)
    ARCH_FLAGS = -mcpu=arm1176jzf-s -mfpu=vfp -mfloat-abi=hard
    PERIPHERAL_BASE = 0x20000000
endif

ifeq ($(PLATFORM),bcm2836)
    ARCH_FLAGS = -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard
    PERIPHERAL_BASE = 0x3F000000
endif

ifeq ($(PLATFORM),bcm2837)
    ARCH_FLAGS = -mcpu=cortex-a53 -mfpu=neon-fp-armv8 -mfloat-abi=hard
    PERIPHERAL_BASE = 0x3F000000
endif

# Compiler flags
CFLAGS = -Wall -Wextra -Werror -O2 -nostdlib -nostartfiles -ffreestanding
CFLAGS += $(ARCH_FLAGS)
CFLAGS += -I$(INCLUDE_DIR) -I$(INCLUDE_DIR)/kernel -I$(INCLUDE_DIR)/drivers
CFLAGS += -I$(INCLUDE_DIR)/services -I$(INCLUDE_DIR)/userspace
CFLAGS += -DPLATFORM_$(shell echo $(PLATFORM) | tr a-z A-Z)
CFLAGS += -DPERIPHERAL_BASE=$(PERIPHERAL_BASE)

# Assembler flags
ASFLAGS = $(ARCH_FLAGS)

# Linker flags
LDFLAGS = -T linker.ld -nostdlib

# Source files
KERNEL_SOURCES = $(wildcard $(SRC_KERNEL)/*/*.c) $(wildcard $(SRC_KERNEL)/*/*/*.c)
KERNEL_ASM = $(wildcard $(SRC_KERNEL)/*/*.S)
DRIVER_SOURCES = $(wildcard $(SRC_DRIVERS)/*/*.c)
SERVICE_SOURCES = $(wildcard $(SRC_SERVICES)/*/*.c)
USERSPACE_SOURCES = $(wildcard $(SRC_USERSPACE)/*/*.c)

# Object files
KERNEL_OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(KERNEL_SOURCES))
KERNEL_OBJS += $(patsubst %.S,$(BUILD_DIR)/%.o,$(KERNEL_ASM))
DRIVER_OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(DRIVER_SOURCES))
SERVICE_OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(SERVICE_SOURCES))
USERSPACE_OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(USERSPACE_SOURCES))

ALL_OBJS = $(KERNEL_OBJS) $(DRIVER_OBJS) $(SERVICE_OBJS) $(USERSPACE_OBJS)

# Target kernel image
KERNEL_ELF = $(BUILD_DIR)/uos.elf
KERNEL_IMG = $(BUILD_DIR)/kernel8.img

# Default target
all: $(KERNEL_IMG)

# Create build directories
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)/$(SRC_KERNEL)/arch
	@mkdir -p $(BUILD_DIR)/$(SRC_KERNEL)/core
	@mkdir -p $(BUILD_DIR)/$(SRC_KERNEL)/memory
	@mkdir -p $(BUILD_DIR)/$(SRC_KERNEL)/process
	@mkdir -p $(BUILD_DIR)/$(SRC_DRIVERS)/uart
	@mkdir -p $(BUILD_DIR)/$(SRC_DRIVERS)/framebuffer
	@mkdir -p $(BUILD_DIR)/$(SRC_DRIVERS)/timer
	@mkdir -p $(BUILD_DIR)/$(SRC_DRIVERS)/gpio
	@mkdir -p $(BUILD_DIR)/$(SRC_DRIVERS)/mmc
	@mkdir -p $(BUILD_DIR)/$(SRC_SERVICES)/terminal
	@mkdir -p $(BUILD_DIR)/$(SRC_SERVICES)/filesystem
	@mkdir -p $(BUILD_DIR)/$(SRC_SERVICES)/network
	@mkdir -p $(BUILD_DIR)/$(SRC_SERVICES)/security
	@mkdir -p $(BUILD_DIR)/$(SRC_USERSPACE)/shell
	@mkdir -p $(BUILD_DIR)/$(SRC_USERSPACE)/commands
	@mkdir -p $(BUILD_DIR)/$(SRC_USERSPACE)/utilities

# Compile C files
$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	@echo "CC $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Compile assembly files
$(BUILD_DIR)/%.o: %.S | $(BUILD_DIR)
	@echo "AS $<"
	@$(AS) $(ASFLAGS) $< -o $@

# Link kernel
$(KERNEL_ELF): $(ALL_OBJS)
	@echo "LD $@"
	@$(LD) $(LDFLAGS) $(ALL_OBJS) -o $@

# Create binary image
$(KERNEL_IMG): $(KERNEL_ELF)
	@echo "OBJCOPY $@"
	@$(OBJCOPY) -O binary $< $@
	@echo "Build complete: $@"
	@echo "Target: $(PLATFORM)"
	@ls -lh $@

# Disassembly for debugging
$(BUILD_DIR)/uos.asm: $(KERNEL_ELF)
	@$(OBJDUMP) -D $< > $@

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR)

# Platform-specific builds
bcm2835:
	$(MAKE) PLATFORM=bcm2835

bcm2836:
	$(MAKE) PLATFORM=bcm2836

bcm2837:
	$(MAKE) PLATFORM=bcm2837

# Help target
help:
	@echo "UOS Build System"
	@echo "================"
	@echo "Targets:"
	@echo "  all         - Build kernel image (default)"
	@echo "  bcm2835     - Build for Raspberry Pi 0/1"
	@echo "  bcm2836     - Build for Raspberry Pi 2"
	@echo "  bcm2837     - Build for Raspberry Pi 3/4"
	@echo "  clean       - Remove build artifacts"
	@echo "  help        - Show this message"
	@echo ""
	@echo "Variables:"
	@echo "  PLATFORM    - Target platform (default: bcm2837)"
	@echo "  CROSS_COMPILE - Toolchain prefix (default: arm-none-eabi-)"

.PHONY: all clean bcm2835 bcm2836 bcm2837 help
