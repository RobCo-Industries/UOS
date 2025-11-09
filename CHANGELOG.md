# Changelog

All notable changes to the Unified Operating System (UOS) project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [7.0.2.8] - 2077-10-23 (Initial Implementation)

### Added

#### Kernel Core
- ARM assembly bootstrap code (boot.S)
- Kernel initialization and main loop
- Memory management with bump allocator
- Memory utility functions (memset, memcpy, memcmp)
- Flat memory model (1MB kernel, 1MB services, 13MB user space)

#### Device Drivers
- UART driver for serial console (115200 baud)
- Framebuffer driver for display output (640x480, 32-bit)
- System timer driver with microsecond precision
- Delay functions (microseconds, milliseconds, seconds)

#### Terminal System
- Terminal service with command interpreter
- Login system with authentication
- User session management
- Command input with echo and backspace support
- OpenVMS-style command processing (case-insensitive)

#### Security System
- Three default user accounts (ADMIN, GUEST, MAINT)
- Username/password authentication
- Maintenance mode vulnerability (SET HALT RESTART/MAINT)
- Password dump to memory address 0x000FFF00
- Intentionally weak security (MD5, no salt)

#### Commands
- **HELP** (?) - Display command reference
- **DIRECTORY** (DIR) - List files (simulated)
- **TYPE** - Display file contents (stub)
- **COPY** - Copy files (stub)
- **DELETE** (DEL) - Remove files (stub)
- **RENAME** - Rename files (stub)
- **SET** - Configure system settings (including exploit)
- **SHOW** - Display system information
- **RUN** - Execute programs (stub)
- **LOGOUT** - End session
- **EXIT** - End session (alias)
- **ROBCO** - Display RobCo system information

#### Build System
- Makefile with multi-platform support (BCM2835, BCM2836, BCM2837)
- Linker script for proper memory layout
- Cross-compilation support (arm-none-eabi)
- Platform-specific compiler flags
- Build artifact management

#### Documentation
- Comprehensive README with features and installation
- BUILD.md with detailed build instructions
- INSTALLATION.md with SD card setup guide
- USER_GUIDE.md with complete command reference
- ARCHITECTURE.md with technical documentation
- CONTRIBUTING.md with contribution guidelines
- Internal memo (lore document dated 2077-10-23)
- Sample config.txt for Raspberry Pi

#### Configuration
- UOS configuration header (uos_config.h)
- Type definitions (types.h)
- Platform-specific peripheral base addresses
- Version information and build constants
- .gitignore for build artifacts

### Implementation Details

**Platform Support:**
- Raspberry Pi Zero/1 (BCM2835)
- Raspberry Pi 2 (BCM2836)
- Raspberry Pi 3/4 (BCM2837)

**Memory Layout:**
- Kernel: 0x00100000 - 0x001FFFFF (1MB)
- Services: 0x00200000 - 0x002FFFFF (1MB)
- User Space: 0x00300000 - 0x00FFFFFF (13MB)
- Applications: 0x01000000+ (remaining RAM)

**Boot Sequence:**
1. RETROS-BIOS (first-stage bootloader)
2. MFBootAgent (second-stage boot manager)
3. UOS Kernel initialization
4. Service startup
5. Terminal login prompt

**Authentic Features:**
- Boot animation with delays
- OpenVMS-style command syntax
- Security vulnerabilities by design
- Lore-accurate error messages
- Pre-war (2077) dated content

### Known Issues

These are **intentional design features** for Fallout authenticity:

1. **Security Vulnerability**: SET HALT RESTART/MAINT dumps passwords
   - Status: By design, will not fix
   - Location: Memory address 0x000FFF00
   - Credentials: Plaintext user:password pairs

2. **Weak Password Storage**: MD5 hashing without salt
   - Status: By design, will not fix
   - Purpose: Authentic to Fallout universe

3. **Buffer Overflow Potential**: Login input lacks bounds checking
   - Status: Under investigation
   - Note: May be exploitable (authentic to source material)

### Future Work

**Planned for v7.1+:**
- File system implementation (FAT32)
- Complete command implementations (TYPE, COPY, DELETE, RENAME)
- EDIT command for text editing
- Network stack (TCP/IP)
- Multi-terminal support (12 concurrent sessions)
- CRT visual effects (scanlines, phosphor decay, flicker)
- Sound effects (keyboard clicks, beeps)
- Command history and tab completion
- Process management
- More easter eggs and lore

### Dependencies

**Build Dependencies:**
- arm-none-eabi-gcc (ARM cross-compiler)
- arm-none-eabi-binutils (ARM binary utilities)
- make (build system)

**Runtime Dependencies:**
- Raspberry Pi firmware (bootcode.bin, start.elf, fixup.dat)
- RETROS-BIOS (first-stage bootloader)
- MFBootAgent (second-stage boot manager)
- SD card formatted as FAT32
- HDMI display
- USB keyboard (via serial console)

### Credits

**Development Team:**
- Kernel architecture and implementation
- Device drivers
- Terminal system
- Security system (with flaws)
- Documentation

**Inspired By:**
- Fallout game series (Bethesda Game Studios)
- OpenVMS operating system (command syntax)
- Raspberry Pi bare-metal programming community

### License

MIT License - See LICENSE file for details

Fallout® is a registered trademark of Bethesda Softworks LLC.
This is a fan project for educational and entertainment purposes.

---

## Version History

### Semantic Versioning

UOS follows the version format: MAJOR.MINOR.PATCH.BUILD

- **7.0.2.8** - Initial release (2077-10-23)
- **7.1.x.x** - Planned: File system and networking (Q1 2078)
- **8.0.x.x** - Planned: Major redesign (Q3 2078)

*Note: Dates are lore-accurate (pre-war 2077)*

---

**RobCo Industries™** - *"The Future of Computing, Yesterday!"*

*For technical support, see INSTALLATION.md*
*For usage instructions, see USER_GUIDE.md*
*For development info, see CONTRIBUTING.md*
