# UOS User Guide
**Unified Operating System Command Reference**

## Introduction

Welcome to the Unified Operating System (UOS), RobCo Industries' flagship terminal operating system. This guide covers all available commands and system features.

## Command Syntax

UOS uses OpenVMS-inspired command syntax:

```
VERB[/QUALIFIER] PARAMETER
```

**Key Features:**
- **Case Insensitive**: Commands work in any case (HELP, help, Help)
- **Abbreviations**: Commands can be shortened to unique prefix (DIR for DIRECTORY)
- **Qualifiers**: Options prefixed with `/` (e.g., `/OUTPUT`)
- **Parameters**: Space-separated arguments

## Getting Started

### Logging In

At the login prompt, enter credentials:

```
Username: ADMIN
Password: ADMIN
```

**Default Accounts:**
- `ADMIN/ADMIN` - Full system access
- `GUEST/GUEST` - Limited access
- `MAINT/MAINT` - Maintenance access

### Your First Commands

```
UOS> HELP              # Show available commands
UOS> ROBCO             # Display system information
UOS> SHOW SYSTEM       # Show system status
UOS> DIRECTORY         # List files
```

## Command Reference

### Help Commands

#### HELP or ?
Display command reference

**Syntax:**
```
HELP [command]
? [command]
```

**Examples:**
```
UOS> HELP              # Show all commands
UOS> ?                 # Same as HELP
UOS> HELP DIRECTORY    # Help for specific command (not yet implemented)
```

---

### File Management Commands

#### DIRECTORY (DIR)
List files and directories

**Syntax:**
```
DIRECTORY [path]
DIR [path]
```

**Examples:**
```
UOS> DIRECTORY         # List current directory
UOS> DIR               # Short form
UOS> DIR /SYSTEM       # List system directory
```

**Output Format:**
```
Directory of /SYSTEM

CONFIG.SYS;1        1024  2077-10-23 09:15
USERS.DB;1          2048  2077-10-23 09:15
TERMINAL.CONF;1      512  2077-10-23 09:15

Total of 3 files, 3584 blocks
```

**File Format:**
- `FILENAME.EXT;VERSION` - File with version number
- Size in bytes
- Last modified date and time

#### TYPE
Display file contents

**Syntax:**
```
TYPE filename
```

**Examples:**
```
UOS> TYPE CONFIG.SYS;1
UOS> TYPE README.TXT
```

**Note:** Currently returns "File not found" for most files (implementation pending).

#### COPY
Copy files

**Syntax:**
```
COPY source destination
```

**Examples:**
```
UOS> COPY FILE1.TXT FILE2.TXT
UOS> COPY /SYSTEM/CONFIG.SYS /BACKUP/
```

**Status:** Not yet implemented

#### DELETE (DEL)
Remove files

**Syntax:**
```
DELETE filename
DEL filename
```

**Examples:**
```
UOS> DELETE TEMP.DAT;1
UOS> DEL *.TMP         # Delete all .TMP files (wildcard support pending)
```

**Status:** Not yet implemented

#### RENAME
Rename files

**Syntax:**
```
RENAME oldname newname
```

**Examples:**
```
UOS> RENAME OLD.TXT NEW.TXT
```

**Status:** Not yet implemented

---

### System Commands

#### SET
Configure system settings

**Syntax:**
```
SET option value
```

**Options:**
- `TERMINAL` - Terminal settings
- `FILE` - File attributes
- `PROCESS` - Process configuration
- `HALT RESTART/MAINT` - **Maintenance mode exploit**

**Examples:**
```
UOS> SET TERMINAL /WIDTH:80
UOS> SET FILE/PROTECTION=(OWNER:RWED) FILE.TXT
UOS> SET HALT RESTART/MAINT    # Security vulnerability!
```

**Special: Maintenance Mode Exploit**
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

This is the classic Fallout terminal hacking feature!

#### SHOW
Display system information

**Syntax:**
```
SHOW [option]
```

**Options:**
- `SYSTEM` - System status
- `TERMINAL` - Terminal configuration
- `MEMORY` - Memory usage
- `NETWORK` - Network status
- `PROCESS` - Running processes

**Examples:**
```
UOS> SHOW SYSTEM
System Status:
==============
UOS Version:     v7.0.2.8
Kernel:          UOSKRNL.303
Uptime:          Running
Memory:          Available
Terminal:        Active
```

```
UOS> SHOW TERMINAL
UOS> SHOW MEMORY
UOS> SHOW NETWORK
```

#### RUN
Execute a program

**Syntax:**
```
RUN program [arguments]
```

**Examples:**
```
UOS> RUN DIAGNOSTIC.EXE
UOS> RUN /PROGRAMS/UTILITY.EXE /DEBUG
```

**Status:** Not yet implemented

---

### Session Commands

#### LOGIN
User authentication

**Syntax:**
```
LOGIN
```

**Note:** LOGIN is automatic at system start. This command would be used to switch users.

#### LOGOUT or EXIT
End current session

**Syntax:**
```
LOGOUT
EXIT
```

**Examples:**
```
UOS> LOGOUT

Logging out...
Session terminated.
System halted. Please reset.
```

Both commands do the same thing - terminate your session.

---

### RobCo Special Commands

#### ROBCO
Display RobCo system information

**Syntax:**
```
ROBCO [/VERSION]
```

**Example:**
```
UOS> ROBCO

================================================================================
                         ROBCO INDUSTRIES (TM)
                  UNIFIED OPERATING SYSTEM v7.0.2.8
================================================================================

  Kernel Version:    UOSKRNL.303
  Copyright:         2075-2077 RobCo Ind.
  Build Date:        2077-10-23
  Target Platform:   ARM Cortex-A Series

  "The Future of Computing, Yesterday!"

================================================================================
```

---

## Advanced Features

### Command Abbreviations

UOS supports command abbreviations. Use the shortest unique prefix:

```
UOS> H          # HELP
UOS> DIR        # DIRECTORY
UOS> T FILE.TXT # TYPE FILE.TXT
UOS> SHO SYS    # SHOW SYSTEM
```

### Case Insensitivity

All commands work in any case:

```
UOS> help
UOS> HELP
UOS> Help
UOS> HeLp
```

All produce the same result.

### Command Qualifiers

Many commands support qualifiers (options) prefixed with `/`:

```
UOS> DIRECTORY/SIZE
UOS> COPY/REPLACE FILE1.TXT FILE2.TXT
UOS> SET TERMINAL/WIDTH:132
```

**Status:** Full qualifier support pending

### File Versioning

UOS supports automatic file versioning:

```
FILE.TXT;1    # First version
FILE.TXT;2    # Second version
FILE.TXT;3    # Third version
```

Operations:
- Creating a file generates version ;1
- Editing creates next version
- DIRECTORY shows all versions
- Default operation uses highest version

---

## File System Structure

```
/
├── system/          # System files (read-only)
│   ├── config.sys
│   ├── users.db
│   ├── terminal.conf
│   └── network.conf
├── users/           # User home directories
│   ├── admin/
│   ├── guest/
│   └── maint/
├── programs/        # Installed programs
├── data/            # Application data
├── temp/            # Temporary files
├── logs/            # System logs
└── holotapes/       # External media mount point
```

---

## File Types

| Extension | Type | Description |
|-----------|------|-------------|
| .TXT      | Text | Plain text files |
| .DAT      | Data | Binary data files |
| .EXE      | Executable | Programs |
| .HLP      | Help | Help files |
| .LOG      | Log | System logs |
| .CFG      | Config | Configuration |
| .SYS      | System | System files |
| .HOL      | Holotape | External media |

---

## Security and Access Control

### Access Levels

| User | Access Level | Capabilities |
|------|--------------|--------------|
| ADMIN | Full | All commands, all files |
| GUEST | Limited | Read-only system files |
| MAINT | Maintenance | Diagnostic access |

### File Protection

Files have protection codes:
```
OWNER:RWED, GROUP:RE, WORLD:R
```

- **R** - Read
- **W** - Write
- **E** - Execute
- **D** - Delete

### Security Vulnerabilities

**By Design**: UOS contains intentional security flaws:

1. **Weak Passwords**: Default accounts use obvious passwords
2. **Maintenance Mode Exploit**: `SET HALT RESTART/MAINT` dumps credentials
3. **No Encryption**: Passwords stored with weak MD5 hashing
4. **Memory Access**: Credentials written to predictable memory location

These are authentic to the Fallout universe!

---

## Error Messages

### Common Error Codes

| Code | Meaning |
|------|---------|
| 0x00000000 | Success |
| 0x03C663A1 | Network connection lost |
| 0xFFF7329A | Data corruption detected |
| 0xC00FE321 | Processor exception |
| 0xDEADBEEF | System panic |

### Error Format

```
% ERROR - Description
  Error code: 0xHHHHHHHH
  Additional information...
```

### Common Messages

**File Not Found:**
```
% File not found or access denied
```

**Invalid Command:**
```
% Invalid command: BADCOMMAND
Type HELP for a list of commands.
```

**Access Denied:**
```
% Insufficient privileges for this operation
```

---

## Terminal Features

### Display Settings

- **Width**: 80 columns (default), configurable
- **Height**: 24 rows
- **Colors**: Green phosphor (default), amber, blue, white

### Special Keys

| Key | Function |
|-----|----------|
| Enter | Execute command |
| Backspace | Delete character |
| Ctrl+C | Interrupt (future) |
| Alt+F1-F12 | Switch terminals (future) |

### CRT Effects

- **Scanlines**: Authentic CRT appearance
- **Phosphor Glow**: Character persistence
- **Cursor Blink**: 500ms rate
- **Screen Flicker**: Occasional (authentic)

---

## Network Features (Future)

### Remote Terminal Access

```
UOS> SET HOST terminal.robco.local
```

### File Transfer

```
UOS> COPY/FTP LOCAL.TXT REMOTE::FILE.TXT
```

### Network Status

```
UOS> SHOW NETWORK
```

---

## Tips and Tricks

### Command History

*Not yet implemented* - Will allow up/down arrows to recall commands

### Tab Completion

*Not yet implemented* - Will complete file and command names

### Wildcards

*Pending implementation*:
```
UOS> DIR *.TXT         # All text files
UOS> DELETE TEMP*.*;*  # All TEMP files, all versions
```

### Piping

*Future feature*:
```
UOS> DIRECTORY | FIND "2077"
UOS> TYPE FILE.TXT | MORE
```

---

## Troubleshooting

### Terminal Not Responding

1. Check keyboard connection
2. Try serial console
3. Reboot system

### Commands Not Working

1. Check spelling (but remember case-insensitive)
2. Type HELP to see available commands
3. Some features not yet implemented

### Display Issues

1. Adjust config.txt framebuffer settings
2. Try different display
3. Check HDMI connection

---

## Easter Eggs

UOS contains hidden features and easter eggs. Try:

- Typing "GARY" repeatedly
- Accessing hidden memory addresses
- Special key combinations
- Exploring system files
- Looking for developer credits

*Hint: Some are not yet implemented!*

---

## Getting Help

### Built-in Help
```
UOS> HELP              # Command list
UOS> ? COMMAND         # Command-specific help (pending)
```

### Documentation
- README.md - Project overview
- BUILD.md - Build instructions
- INSTALLATION.md - Setup guide
- This file - User guide

### Support
- GitHub Issues
- Community forums
- Discord server

---

## Version History

### v7.0.2.8 (Current)
- Initial release
- Core commands
- Terminal interface
- Maintenance mode exploit
- Login system

### Future Versions
- Full file system
- Network stack
- More commands
- Easter eggs
- Performance improvements

---

**RobCo Industries™** - *"The Future of Computing, Yesterday!"*

*UOS v7.0.2.8 - User Guide*
*Copyright 2075-2077 RobCo Industries*
