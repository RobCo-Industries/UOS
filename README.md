# UOS - Unified Operating System
**RobCo Industries Terminal Operating System for ARM Architecture**

![Version](https://img.shields.io/badge/version-7.0.2.8-green)
![Kernel](https://img.shields.io/badge/kernel-UOSKRNL.303-blue)
![Platform](https://img.shields.io/badge/platform-ARM-orange)
![License](https://img.shields.io/badge/license-MIT-lightgrey)

The Unified Operating System (UOS) is RobCo Industries' flagship terminal operating system, faithfully recreated for modern ARM hardware (Raspberry Pi). This implementation creates a fully functional, lore-accurate terminal system inspired by the Fallout video game series.

## Features

### Core System
- **Microkernel Design**: Minimal kernel with modular services
- **Memory Management**: Flat memory model with efficient allocation
- **Terminal System**: OpenVMS-inspired command interpreter
- **Multi-Terminal Support**: Up to 12 concurrent terminal sessions
- **Authentic Interface**: CRT effects, phosphor display simulation

### Terminal Interface
- **80x24 Character Display**: Classic terminal dimensions
- **Color Schemes**: Green phosphor (default), amber, blue, white on black
- **CRT Effects**: Scanlines, phosphor persistence, authentic feel
- **RobCo Termlink Protocol**: VT100 compatible with extensions

### Security System (Intentionally Flawed)
The security system contains deliberate vulnerabilities for authentic Fallout gameplay:
- **Weak Authentication**: Default accounts (ADMIN/ADMIN, GUEST/GUEST, MAINT/MAINT)
- **Maintenance Mode Exploit**: `SET HALT RESTART/MAINT` dumps passwords
- **Memory Access**: Credentials dumped to 0x000FFF00

### Command System
OpenVMS-style command syntax with RobCo modifications:

**File Management:**
- `DIRECTORY` or `DIR` - List files and directories
- `TYPE` - Display file contents
- `COPY` - Copy files
- `DELETE` or `DEL` - Remove files
- `RENAME` - Rename files

**System Control:**
- `SET` - Configure system settings
- `SHOW` - Display system information
- `RUN` - Execute programs
- `LOGIN` - User authentication
- `LOGOUT` - End session

**Special Commands:**
- `ROBCO` - Display system information
- `HELP` or `?` - Command help
- `SET HALT RESTART/MAINT` - Maintenance mode exploit

## Hardware Support

### Supported Platforms
- **BCM2835**: Raspberry Pi Zero, Zero W, 1A, 1B, 1B+
- **BCM2836**: Raspberry Pi 2B
- **BCM2837**: Raspberry Pi 3B, 3B+, 4B (in 32-bit mode)

### Peripherals
- **Display**: HDMI output (640x480)
- **Input**: USB keyboard via UART console
- **Storage**: SD card
- **Serial**: UART0 (GPIO 14/15) at 115200 baud

## Building

### Prerequisites
Install the ARM cross-compiler toolchain:

```bash
# Ubuntu/Debian
sudo apt-get install gcc-arm-none-eabi

# macOS with Homebrew
brew install --cask gcc-arm-embedded

# Arch Linux
sudo pacman -S arm-none-eabi-gcc
```

### Build Commands

Build for Raspberry Pi 3/4 (default):
```bash
make
```

Build for specific platforms:
```bash
make bcm2835  # Raspberry Pi 0/1
make bcm2836  # Raspberry Pi 2
make bcm2837  # Raspberry Pi 3/4
```

Clean build artifacts:
```bash
make clean
```

The build output is `build/kernel8.img` which needs to be copied to your SD card.

## Installation

### SD Card Preparation

1. **Format SD Card**: FAT32 filesystem
2. **Copy Boot Files**: You need these from the [Raspberry Pi firmware](https://github.com/raspberrypi/firmware/tree/master/boot):
   - `bootcode.bin` (for Pi 0/1)
   - `start.elf`
   - `fixup.dat`

3. **Copy RETROS-BIOS**: Get `kernel.img` from [RETROS-BIOS](https://github.com/RobCo-Industries/RETROS-BIOS)

4. **Copy MFBootAgent**: Get `kernel7.img` or `kernel8.img` from [MfBootAgent](https://github.com/RobCo-Industries/MfBootAgent)

5. **Copy UOS Kernel**: Copy `build/kernel8.img` from this build

6. **Create config.txt**:
```
# Enable UART
enable_uart=1

# Set kernel for boot
kernel=kernel.img  # RETROS-BIOS boots first
```

### Boot Sequence
```
Power On → RETROS-BIOS → MFBootAgent → UOS Kernel → Terminal Login
```

## Usage

### Login
Use one of the default accounts:
- Username: `ADMIN`, Password: `ADMIN` (full access)
- Username: `GUEST`, Password: `GUEST` (limited access)
- Username: `MAINT`, Password: `MAINT` (maintenance access)

### Basic Commands
```
UOS> HELP                    # Show available commands
UOS> DIRECTORY               # List files
UOS> SHOW SYSTEM            # Display system status
UOS> ROBCO                  # Show RobCo information
UOS> SET HALT RESTART/MAINT # Maintenance mode exploit
UOS> LOGOUT                 # Exit session
```

### Maintenance Mode Exploit
The classic Fallout terminal hacking feature:
```
UOS> SET HALT RESTART/MAINT
% WARNING: Maintenance mode initiated
% System will reboot and dump credentials to memory
% Memory address: 0x000FFF00

% Dumping credentials:
  ADMIN:ADMIN
  GUEST:GUEST
  MAINT:MAINT
```

## Development

### Project Structure
```
UOS/
├── kernel/           # Kernel core and initialization
│   ├── arch/        # Architecture-specific code
│   ├── core/        # Core kernel functions
│   ├── memory/      # Memory management
│   └── process/     # Process management (future)
├── drivers/         # Hardware drivers
│   ├── uart/        # Serial communication
│   ├── framebuffer/ # Display driver
│   ├── timer/       # System timer
│   ├── gpio/        # GPIO control (future)
│   └── mmc/         # SD card (future)
├── services/        # System services
│   ├── terminal/    # Terminal service
│   ├── filesystem/  # File system (future)
│   ├── network/     # Networking (future)
│   └── security/    # Authentication (future)
├── userspace/       # User-space applications
│   ├── shell/       # Command shell
│   ├── commands/    # Command implementations
│   └── utilities/   # System utilities (future)
├── include/         # Header files
├── docs/            # Documentation
└── tools/           # Build tools
```

### Memory Layout
```
0x00100000 - 0x001FFFFF : Kernel Space (1MB)
0x00200000 - 0x002FFFFF : System Services (1MB)
0x00300000 - 0x00FFFFFF : User Space (13MB)
0x01000000 - [RAM_END]  : Application Space
```

### Adding New Commands
1. Add command entry to `command_table[]` in `userspace/commands/commands.c`
2. Implement handler function following the pattern `cmd_<name>(const char* args)`
3. Add description for help text
4. Rebuild and test

## Technical Specifications

### Version Information
- **Current Version**: UOS v7.0.2.8
- **Kernel Version**: UOSKRNL.303
- **Copyright**: 2075-2077 RobCo Industries
- **Build Target**: ARM Cortex-A series

### Performance Targets
- **RAM Usage**: < 64MB for core system
- **Storage**: < 100MB base installation
- **Boot Time**: < 30 seconds (with authentic delays)
- **Command Response**: < 100ms

### Error Codes
8-digit hexadecimal format: `0xAABBCCDD`
- `0x00000000` - Success
- `0x03C663A1` - Network connection lost
- `0xFFF7329A` - Data corruption detected
- `0xC00FE321` - Processor exception
- `0xDEADBEEF` - System panic

## Lore Accuracy

This implementation faithfully recreates the terminal experience from the Fallout universe:

- **Authentic Boot Sequence**: References RETROS-BIOS and MFBootAgent
- **OpenVMS-Style Commands**: Case-insensitive, abbreviated commands
- **Security Vulnerabilities**: The famous maintenance mode hack
- **Terminal Effects**: Green phosphor display, CRT aesthetics
- **Error Messages**: Lore-accurate system messages
- **Dated Elements**: Files and logs dated 2077 (pre-war)

## Contributing

Contributions welcome! Areas for enhancement:
- Full filesystem implementation (FAT32/ext2)
- Network stack (TCP/IP)
- Additional terminal effects (flicker, burn-in)
- More commands and utilities
- Easter eggs and hidden features
- Documentation improvements

## License

MIT License - See LICENSE file for details

## Credits

Developed as part of the RobCo Industries terminal system recreation:
- [RETROS-BIOS](https://github.com/RobCo-Industries/RETROS-BIOS) - First-stage bootloader
- [MFBootAgent](https://github.com/RobCo-Industries/MfBootAgent) - Second-stage boot manager
- [UOS](https://github.com/RobCo-Industries/UOS) - This operating system

Inspired by the Fallout video game series by Bethesda Game Studios.

## Disclaimer

This is a fan project for educational and entertainment purposes. All trademarks belong to their respective owners. RobCo Industries and related terms are fictional elements from the Fallout universe.

---

**RobCo Industries™** - *"The Future of Computing, Yesterday!"*
