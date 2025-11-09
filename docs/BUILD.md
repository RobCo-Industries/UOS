# Building UOS

## Prerequisites

### Cross-Compilation Toolchain

UOS requires an ARM cross-compiler to build. Install the appropriate toolchain for your platform:

#### Ubuntu/Debian Linux
```bash
sudo apt-get update
sudo apt-get install gcc-arm-none-eabi binutils-arm-none-eabi
```

#### Fedora/RHEL Linux
```bash
sudo dnf install arm-none-eabi-gcc-cs arm-none-eabi-binutils
```

#### Arch Linux
```bash
sudo pacman -S arm-none-eabi-gcc arm-none-eabi-binutils
```

#### macOS
Using Homebrew:
```bash
brew install --cask gcc-arm-embedded
```

Or download directly from [ARM Developer](https://developer.arm.com/downloads/-/gnu-rm).

#### Windows
Download the installer from [ARM Developer](https://developer.arm.com/downloads/-/gnu-rm) and add to PATH.

### Verify Installation
```bash
arm-none-eabi-gcc --version
```

You should see output similar to:
```
arm-none-eabi-gcc (GNU Arm Embedded Toolchain 10.3-2021.10) 10.3.1 20210824
```

## Building

### Quick Start

Build for Raspberry Pi 3/4 (default):
```bash
make
```

### Platform-Specific Builds

Build for Raspberry Pi Zero/1 (BCM2835):
```bash
make bcm2835
```

Build for Raspberry Pi 2 (BCM2836):
```bash
make bcm2836
```

Build for Raspberry Pi 3/4 (BCM2837):
```bash
make bcm2837
```

### Build Output

After a successful build, you will see:
```
Build complete: build/kernel8.img
Target: bcm2837
-rw-r--r-- 1 user user 15360 Nov 9 12:00 build/kernel8.img
```

The kernel image is located at `build/kernel8.img`.

### Clean Build

Remove all build artifacts:
```bash
make clean
```

### Build Artifacts

The build process creates:
- `build/kernel8.img` - Final kernel image for SD card
- `build/uos.elf` - ELF binary with symbols (for debugging)
- `build/uos.asm` - Disassembly listing (optional, for analysis)
- `build/**/*.o` - Object files (intermediate)

## Platform Differences

### BCM2835 (Raspberry Pi Zero/1)
- **CPU**: ARM1176JZF-S (ARMv6)
- **FPU**: VFP
- **Peripherals**: Base address 0x20000000
- **Kernel Name**: kernel.img

### BCM2836 (Raspberry Pi 2)
- **CPU**: Cortex-A7 (ARMv7-A)
- **FPU**: NEON + VFPv4
- **Peripherals**: Base address 0x3F000000
- **Kernel Name**: kernel7.img

### BCM2837 (Raspberry Pi 3/4)
- **CPU**: Cortex-A53 (ARMv8-A, 32-bit mode)
- **FPU**: NEON + FP-ARMv8
- **Peripherals**: Base address 0x3F000000
- **Kernel Name**: kernel8.img

## Compilation Flags

### Common Flags
- `-Wall -Wextra -Werror` - Enable all warnings and treat as errors
- `-O2` - Optimization level 2
- `-nostdlib -nostartfiles` - No standard library
- `-ffreestanding` - Freestanding environment

### Platform-Specific Flags

**BCM2835:**
```
-mcpu=arm1176jzf-s -mfpu=vfp -mfloat-abi=hard
```

**BCM2836:**
```
-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard
```

**BCM2837:**
```
-mcpu=cortex-a53 -mfpu=neon-fp-armv8 -mfloat-abi=hard
```

## Troubleshooting

### "arm-none-eabi-gcc: command not found"
Install the ARM toolchain as described in Prerequisites.

### "undefined reference to `__aeabi_uidiv`"
The Makefile should automatically link with libgcc. If you see this error:
```bash
make clean
make PLATFORM=bcm2837
```

### Linker Errors
Ensure linker.ld is present and not modified. The memory layout is:
```
Kernel: 0x00100000 (1MB offset)
RAM: 15MB available
```

### Large Binary Size
If the kernel image exceeds 1MB, review:
- Optimization level (should be -O2)
- Unused code/data sections
- Debug symbols (use strip if needed)

### Cross-Platform Build Issues
If building on Windows with WSL or Cygwin:
- Use Unix-style line endings (LF, not CRLF)
- Ensure file permissions are correct
- Use forward slashes in paths

## Makefile Targets

### Primary Targets
- `all` (default) - Build kernel image
- `clean` - Remove build artifacts
- `help` - Display help message

### Platform Targets
- `bcm2835` - Build for Pi 0/1
- `bcm2836` - Build for Pi 2
- `bcm2837` - Build for Pi 3/4

### Variables
Override default settings:
```bash
# Use different toolchain
make CROSS_COMPILE=arm-linux-gnueabihf-

# Build for specific platform
make PLATFORM=bcm2836

# Change optimization
make CFLAGS="-O3 -Wall"
```

## Debugging

### Generate Disassembly
```bash
make build/uos.asm
```

This creates a disassembly listing of the entire kernel.

### Symbol Information
The ELF file contains symbol information:
```bash
arm-none-eabi-nm build/uos.elf | less
arm-none-eabi-objdump -t build/uos.elf | less
```

### Size Analysis
Check section sizes:
```bash
arm-none-eabi-size build/uos.elf
```

Example output:
```
   text    data     bss     dec     hex filename
  12345     256    4096   16697    4139 build/uos.elf
```

## Dependencies

The build system tracks dependencies automatically through GCC's `-MMD` flag (if enabled). Modify the Makefile to enable:

```makefile
CFLAGS += -MMD -MP
-include $(OBJS:.o=.d)
```

## Next Steps

After building:
1. Copy `build/kernel8.img` to your SD card
2. See INSTALLATION.md for SD card setup
3. Boot your Raspberry Pi
4. Enjoy UOS!

---

**RobCo Industries™** - *"The Future of Computing, Yesterday!"*
