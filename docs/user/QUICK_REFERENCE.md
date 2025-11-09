# UOS Quick Reference Card

**Unified Operating System v7.0.2.8**  
*RobCo Industries Terminal Operating System*

---

## Login

**Default Accounts:**
```
ADMIN / ADMIN   (Full access)
GUEST / GUEST   (Limited access)
MAINT / MAINT   (Maintenance access)
```

---

## Essential Commands

| Command | Shortcut | Description |
|---------|----------|-------------|
| HELP | ? | Show command list |
| DIRECTORY | DIR | List files |
| TYPE filename | - | Display file |
| SHOW SYSTEM | SHOW SYS | System status |
| ROBCO | - | System info |
| LOGOUT | EXIT | End session |

---

## File Management

| Command | Description |
|---------|-------------|
| DIRECTORY [path] | List directory contents |
| TYPE filename | Display file contents |
| COPY source dest | Copy file |
| DELETE filename | Remove file |
| RENAME old new | Rename file |

---

## System Commands

| Command | Description |
|---------|-------------|
| SET option value | Configure setting |
| SHOW [option] | Display information |
| RUN program | Execute program |
| ROBCO | RobCo information |

---

## Special Features

### Maintenance Mode Exploit
```
UOS> SET HALT RESTART/MAINT
```
Dumps system passwords to memory address 0x000FFF00

### Command Syntax
- **Case Insensitive**: HELP = help = Help
- **Abbreviations**: DIR = DIRECTORY
- **Qualifiers**: /OPTION after command

---

## File System

```
/
├── system/      System files
├── users/       User directories
├── programs/    Applications
├── data/        Data files
├── temp/        Temporary files
├── logs/        System logs
└── holotapes/   External media
```

---

## File Types

| Extension | Type |
|-----------|------|
| .TXT | Text file |
| .DAT | Data file |
| .EXE | Executable |
| .HLP | Help file |
| .LOG | Log file |
| .CFG | Configuration |
| .SYS | System file |
| .HOL | Holotape |

---

## Error Codes

| Code | Meaning |
|------|---------|
| 0x00000000 | Success |
| 0x03C663A1 | Network error |
| 0xFFF7329A | Data corruption |
| 0xC00FE321 | Processor exception |
| 0xDEADBEEF | System panic |

---

## Keyboard

| Key | Function |
|-----|----------|
| Enter | Execute command |
| Backspace | Delete character |
| Ctrl+C | Interrupt (future) |
| Alt+F1-F12 | Switch terminals (future) |

---

## Tips

- Commands are case-insensitive
- Use TAB for completion (future)
- UP/DOWN for history (future)
- Type HELP for full list
- Wildcards: *.TXT (future)

---

## Serial Console

**Settings:**
- Baud: 115200
- Data: 8 bits
- Parity: None
- Stop: 1 bit
- Flow: None

**GPIO Pins:**
- TX: GPIO 14 (Pin 8)
- RX: GPIO 15 (Pin 10)
- GND: Pin 6

---

## Troubleshooting

**No display?**
- Check HDMI cable
- Try hdmi_safe=1 in config.txt

**No serial output?**
- Check UART wiring
- Verify enable_uart=1 in config.txt

**Boot loop?**
- Check kernel image
- Verify all boot files present

**Commands not working?**
- Check spelling
- Type HELP for available commands

---

## Build Quick Start

```bash
# Install toolchain
sudo apt-get install gcc-arm-none-eabi

# Build for Pi 3
make

# Build for other platforms
make bcm2835  # Pi 0/1
make bcm2836  # Pi 2
make bcm2837  # Pi 3/4

# Clean
make clean
```

---

## SD Card Setup

**Required Files:**
1. bootcode.bin (Pi 0/1 only)
2. start.elf
3. fixup.dat
4. config.txt
5. kernel.img (RETROS-BIOS)
6. kernel8.img (UOS)

**Format:** FAT32

---

## Support

- **Documentation:** docs/user/USER_GUIDE.md
- **Technical:** docs/technical/ARCHITECTURE.md
- **Build Help:** docs/BUILD.md
- **Installation:** docs/INSTALLATION.md

---

## Version Info

```
UOS> ROBCO

UOS Version:  v7.0.2.8
Kernel:       UOSKRNL.303
Copyright:    2075-2077 RobCo Ind.
Platform:     ARM Cortex-A Series
```

---

## Easter Eggs

Hidden features exist! Try:
- Unusual commands
- Special memory addresses
- Key combinations
- System file exploration

---

**RobCo Industries™**

*"The Future of Computing, Yesterday!"*

---

*Quick Reference Card v1.0*  
*Copyright 2075-2077 RobCo Industries*  
*For internal use and authorized personnel only*

---

**Print this card and laminate for easy reference!**
