# UOS Technical Architecture

## System Overview

The Unified Operating System (UOS) is a bare-metal operating system for ARM processors (Raspberry Pi) implementing a microkernel architecture with modular services. This document describes the technical architecture and implementation details.

## Architecture Layers

### Layer 1: Hardware Abstraction
- **Boot Code**: ARM assembly bootstrap
- **Device Drivers**: UART, framebuffer, timer, GPIO, MMC
- **Memory Management**: Physical memory allocation
- **Interrupt Handling**: ARM interrupt controller

### Layer 2: Kernel Core
- **Process Management**: Cooperative multitasking
- **Memory Manager**: Flat memory model, heap allocation
- **Device Manager**: Unified device interface
- **Error Handling**: System-wide error reporting

### Layer 3: System Services
- **Terminal Service**: Command interpreter, multi-session
- **File System Service**: FAT32/ext2 with RobCo extensions
- **Network Service**: TCP/IP stack (simplified)
- **Security Service**: Authentication and access control

### Layer 4: User Space
- **Command Shell**: OpenVMS-style command processor
- **System Utilities**: File operations, diagnostics
- **Application Runtime**: Program execution environment

## Memory Architecture

### Memory Map
```
0x00000000 - 0x000FFFFF : Exception vectors and GPU memory
0x00100000 - 0x001FFFFF : Kernel space (1MB)
0x00200000 - 0x002FFFFF : System services (1MB)
0x00300000 - 0x00FFFFFF : User space (13MB)
0x01000000 - [RAM_END]  : Application space
0x20000000 - 0x20FFFFFF : Peripherals (BCM2835)
0x3F000000 - 0x3FFFFFFF : Peripherals (BCM2836/7)
```

### Heap Management
Simple bump allocator:
- **Start**: End of BSS section
- **Growth**: Upward toward application space
- **Allocation**: 4-byte aligned
- **Free**: Not yet implemented (stub)

### Stack
- **Location**: Below kernel code
- **Size**: 32KB per process
- **Growth**: Downward
- **Protection**: Software-based (no MMU)

## Boot Sequence

### Stage 0: GPU Firmware
1. GPU loads `bootcode.bin`
2. GPU initializes hardware
3. GPU loads `start.elf`
4. GPU reads `config.txt`

### Stage 1: RETROS-BIOS
1. ARM CPU starts at 0x8000
2. UART initialization
3. Framebuffer setup
4. Boot animation
5. Chain-load MFBootAgent

### Stage 2: MFBootAgent
1. Enhanced hardware detection
2. File system scan
3. Kernel selection (optional menu)
4. Load UOS kernel to 0x100000
5. Jump to kernel entry

### Stage 3: UOS Kernel
1. `_start` in boot.S
2. Set stack pointer
3. Clear BSS section
4. Call `kernel_main()`

### Stage 4: Kernel Initialization
```c
kernel_main()
  ├── uart_init()           // Early serial output
  ├── framebuffer_init()    // Display initialization
  ├── print_boot_header()   // Version banner
  ├── boot_sequence_animation()
  ├── initialize_subsystems()
  │   ├── memory_init()
  │   ├── timer_init()
  │   ├── filesystem_init()
  │   ├── network_init()
  │   └── security_init()
  ├── terminal_init()
  └── terminal_main_loop()  // Never returns
```

## Kernel Subsystems

### UART Driver
- **Hardware**: PL011 UART0
- **Baud Rate**: 115200
- **Data Format**: 8N1
- **Buffer**: None (polling mode)
- **Functions**: Init, putc, getc, puts

**Implementation:**
```c
void uart_init(void)
{
    // Configure GPIO 14/15 for UART
    // Set baud rate divisor
    // Enable UART TX/RX
}
```

### Framebuffer Driver
- **Hardware**: VideoCore GPU mailbox
- **Resolution**: 640x480 (configurable)
- **Depth**: 32-bit RGBA
- **Font**: 8x16 (simplified)
- **Colors**: RGBA format

**Initialization:**
```c
framebuffer_init()
  ├── Setup framebuffer request structure
  ├── Send mailbox message
  ├── Wait for GPU response
  └── Map framebuffer memory
```

### Timer Driver
- **Hardware**: BCM System Timer
- **Frequency**: 1 MHz
- **Width**: 64-bit counter
- **Wraparound**: ~584,000 years

**Functions:**
```c
uint64_t timer_get_ticks(void);      // Microsecond counter
void timer_delay_us(uint32_t us);    // Busy-wait delay
void timer_delay_ms(uint32_t ms);    // Millisecond delay
```

### Memory Manager
- **Algorithm**: Bump allocator
- **Alignment**: 4 bytes
- **Tracking**: Simple counter
- **Overhead**: Minimal

**API:**
```c
void* kmalloc(size_t size);          // Allocate memory
void* kzalloc(size_t size);          // Allocate zeroed memory
void kfree(void* ptr);               // Free (stub)
```

## Terminal System

### Terminal Service
Multi-session terminal manager (future):
- Up to 12 concurrent sessions
- Session switching (Alt+F1-F12)
- Per-session state
- Terminal multiplexing

**Current Implementation:**
Single terminal session with:
- Command line input
- Input echo
- Backspace support
- Command history (future)

### Command Processor

**Flow:**
```
Input → Buffer → Parse → Lookup → Execute → Output
```

**Parser:**
1. Convert to uppercase
2. Split command and arguments
3. Tokenize qualifiers
4. Extract parameters

**Dispatcher:**
```c
typedef struct {
    const char* name;
    void (*handler)(const char* args);
    const char* description;
} command_entry_t;
```

### Login System

**Authentication:**
```c
bool authenticate(username, password)
{
    // Hash password (MD5, intentionally weak)
    // Lookup in user database
    // Compare hashes
    // Return success/failure
}
```

**Default Users:**
| Username | Password | Hash (MD5) |
|----------|----------|------------|
| ADMIN | ADMIN | 21232f297a57a5a743894a0e4a801fc3 |
| GUEST | GUEST | 084e0343a0486ff05530df6c705c8bb4 |
| MAINT | MAINT | 6ae92f6a25d47e0e6f45b4f8c7e07b3a |

## File System (Future)

### Structure
```
Superblock
  ├── Magic number
  ├── Version
  ├── Block size
  ├── Total blocks
  └── Root directory inode

Inode Table
  ├── File metadata
  ├── Block pointers
  ├── Permissions
  └── Timestamps

Data Blocks
  ├── File contents
  └── Directory entries
```

### File Operations
```c
int fs_open(const char* path, int flags);
int fs_read(int fd, void* buf, size_t count);
int fs_write(int fd, const void* buf, size_t count);
int fs_close(int fd);
int fs_stat(const char* path, struct stat* st);
```

## Network Stack (Future)

### Protocol Layers
```
Application: Terminal Protocol, FTP
Transport:   TCP, UDP
Network:     IPv4
Link:        Ethernet
Physical:    BCM ethernet/WiFi
```

### Terminal Protocol
- **Port**: 23 (Telnet-like)
- **Format**: ASCII text
- **Control**: VT100 sequences
- **Auth**: Username/password

## Security Model

### Authentication
**Weak by Design:**
- MD5 password hashing
- Plaintext storage option
- No salt
- Dictionary attacks possible

### Authorization
**File Permissions:**
```
OWNER:RWED, GROUP:RE, WORLD:R
```

**Access Control:**
- User-based permissions
- Group membership
- World access

### Vulnerabilities (Intentional)

#### 1. Maintenance Mode Exploit
```c
if (strcmp(command, "SET HALT RESTART/MAINT") == 0) {
    // Dump passwords to memory
    char* dump = (char*)0x000FFF00;
    strcpy(dump, "ADMIN:ADMIN\n");
    strcpy(dump + 16, "GUEST:GUEST\n");
    strcpy(dump + 32, "MAINT:MAINT\n");
}
```

#### 2. Buffer Overflow
```c
char username[32];
// No bounds checking on input
scanf("%s", username);  // Vulnerable!
```

#### 3. Weak Session Tokens
```c
session_token = time() ^ user_id;  // Predictable!
```

## Error Handling

### Error Code Format
```
0xAABBCCDD
  ││││││└└─ Additional info
  ││││└└─── Specific error
  ││└└───── Error category
  └└──────── Subsystem ID
```

### Subsystem IDs
- 0x01: Kernel
- 0x02: Security
- 0x03: Network
- 0x04: File system
- 0x05: Terminal

### Error Categories
- 0x01: Invalid parameter
- 0x02: Access denied
- 0x03: Not found
- 0x04: Timeout
- 0xFF: Critical error

## Performance Characteristics

### Boot Performance
- RETROS-BIOS: 3-5 seconds
- MFBootAgent: 1-2 seconds
- UOS Init: 2-3 seconds
- **Total**: 6-10 seconds

### Runtime Performance
- **Command Response**: <100ms typical
- **Terminal Refresh**: 60 Hz
- **Memory Allocation**: O(1)
- **Command Lookup**: O(n), n=16

### Resource Usage
- **RAM**: ~10MB kernel + services
- **Storage**: ~50KB kernel image
- **CPU**: <5% idle

## Platform-Specific Details

### BCM2835 (Pi Zero/1)
- **CPU**: ARM1176JZF-S @ 700 MHz
- **RAM**: 256-512 MB
- **Peripherals**: 0x20000000
- **Limitations**: No NEON, slower

### BCM2836 (Pi 2)
- **CPU**: 4x Cortex-A7 @ 900 MHz
- **RAM**: 1 GB
- **Peripherals**: 0x3F000000
- **Features**: Quad-core, NEON

### BCM2837 (Pi 3/4)
- **CPU**: 4x Cortex-A53 @ 1.2 GHz
- **RAM**: 1-8 GB
- **Peripherals**: 0x3F000000
- **Features**: 64-bit capable, fast

## Build System

### Makefile Structure
```makefile
# Platform detection
PLATFORM ?= bcm2837

# Toolchain
CROSS_COMPILE = arm-none-eabi-

# Flags
CFLAGS = -Wall -O2 -nostdlib -ffreestanding
CFLAGS += $(ARCH_FLAGS) -I$(INCLUDE_DIR)

# Targets
all: $(KERNEL_IMG)
```

### Linker Script
```ld
ENTRY(_start)

SECTIONS {
    .text : { *(.text.boot) *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) *(COMMON) }
}
```

## Debug Support

### Serial Debug
All kernel output goes to UART:
```c
#define DEBUG_PRINT(fmt, ...) \
    uart_printf("[%s:%d] " fmt, __FILE__, __LINE__, ##__VA_ARGS__)
```

### Memory Dumps
```c
void dump_memory(void* addr, size_t len)
{
    uint8_t* p = (uint8_t*)addr;
    for (size_t i = 0; i < len; i++) {
        uart_printf("%02x ", p[i]);
        if ((i + 1) % 16 == 0) uart_puts("\r\n");
    }
}
```

### Panic Handler
```c
void kernel_panic(const char* msg, uint32_t code)
{
    uart_puts("\r\n*** KERNEL PANIC ***\r\n");
    uart_puts(msg);
    uart_printf("\r\nError code: 0x%08x\r\n", code);
    while(1) { __asm__ volatile("wfe"); }
}
```

## Future Enhancements

### Short Term
- Complete file system implementation
- Network stack basics
- More commands
- Process management

### Medium Term
- Multi-terminal support
- USB keyboard input
- SD card file system
- Command history

### Long Term
- MMU support
- Preemptive multitasking
- Full network protocols
- Graphics mode
- Sound effects

## References

- ARM Architecture Reference Manual
- BCM2835/6/7 ARM Peripherals Guide
- Raspberry Pi Firmware Documentation
- OpenVMS DCL Commands Reference
- Fallout Terminal Documentation (fan-made)

---

**RobCo Industries™** - *"The Future of Computing, Yesterday!"*

*Technical Architecture Document*
*Version 1.0 - 2077-10-23*
