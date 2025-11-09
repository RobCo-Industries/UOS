# Installing UOS on Raspberry Pi

This guide covers the complete installation process for UOS on Raspberry Pi hardware.

## Prerequisites

- Raspberry Pi (Zero, 1, 2, 3, or 4)
- microSD card (minimum 256MB, recommended 1GB+)
- SD card reader
- USB keyboard
- HDMI display
- USB-to-Serial adapter (optional, for debugging)
- Built UOS kernel image (see BUILD.md)

## SD Card Preparation

### 1. Format SD Card

Format the SD card with FAT32 filesystem:

#### Windows
Use the official SD Card Formatter tool:
https://www.sdcard.org/downloads/formatter/

#### Linux
```bash
# Find your SD card device
lsblk

# Format (replace sdX with your device)
sudo mkfs.vfat -F 32 -n BOOT /dev/sdX1
```

#### macOS
```bash
# Find your SD card
diskutil list

# Format (replace diskN with your device)
sudo diskutil eraseDisk FAT32 BOOT /dev/diskN
```

### 2. Download Raspberry Pi Firmware

Download the official Raspberry Pi boot files from:
https://github.com/raspberrypi/firmware/tree/master/boot

**Required files:**
- `bootcode.bin` (only for Pi 0/1)
- `start.elf`
- `fixup.dat`

Copy these files to the root of your SD card.

### 3. Install RETROS-BIOS

Download the RETROS-BIOS kernel from:
https://github.com/RobCo-Industries/RETROS-BIOS/releases

Copy the appropriate kernel file:
- For Pi 0/1: `kernel.img`
- For Pi 2: `kernel7.img`
- For Pi 3/4: `kernel8.img`

**Important:** Rename it to `kernel.img` as it will be the first-stage bootloader.

### 4. Install MFBootAgent

Download the MFBootAgent kernel from:
https://github.com/RobCo-Industries/MfBootAgent/releases

Copy as `kernel7.img` (for Pi 2) or `kernel8.img` (for Pi 3/4).

### 5. Install UOS Kernel

Copy your built `build/kernel8.img` file to the SD card root.

**Note:** The actual filename depends on your boot chain configuration.

### 6. Create config.txt

Create a `config.txt` file in the root of the SD card with the following content:

```ini
# Raspberry Pi Configuration for UOS

# Enable UART for serial console
enable_uart=1
uart_2ndstage=1

# Kernel selection
# RETROS-BIOS boots first
kernel=kernel.img

# Display settings
framebuffer_width=640
framebuffer_height=480
framebuffer_depth=32

# Disable rainbow splash (for authenticity)
disable_splash=1

# Enable bootloader UART output
bootloader_uart=1
```

### 7. Optional: cmdline.txt

Create `cmdline.txt` for kernel command-line parameters:

```
console=serial0,115200 console=tty1 root=/dev/mmcblk0p2 rootfstype=ext4 elevator=deadline fsck.repair=yes rootwait
```

## Final SD Card Structure

Your SD card should now contain:

```
/
├── bootcode.bin      (Pi 0/1 only)
├── start.elf         (GPU firmware)
├── fixup.dat         (GPU firmware)
├── config.txt        (Configuration)
├── cmdline.txt       (Optional)
├── kernel.img        (RETROS-BIOS)
├── kernel7.img       (MFBootAgent for Pi 2)
├── kernel8.img       (UOS or MFBootAgent for Pi 3/4)
└── ...               (other boot files)
```

## Boot Sequence

The complete boot chain is:

```
Power On
    ↓
GPU Firmware (bootcode.bin + start.elf)
    ↓
RETROS-BIOS (kernel.img)
    ↓
MFBootAgent (kernel7.img or kernel8.img)
    ↓
UOS Kernel (loaded by MFBootAgent)
    ↓
Terminal Login
```

## First Boot

### What to Expect

1. **RETROS-BIOS Boot** (3-5 seconds)
   - Boot beep (three tones)
   - RobCo Industries logo
   - Memory test animation
   - Possible "bad sector" warnings (authentic feature)

2. **MFBootAgent** (1-2 seconds)
   - Boot menu (if multiple OS detected)
   - Loading UOS kernel

3. **UOS Boot** (2-3 seconds)
   - Kernel initialization messages
   - Service startup
   - Terminal initialization

4. **Login Screen**
   ```
   --------------------------------------------------------------------------------
                         ROBCO INDUSTRIES UNIFIED OPERATING SYSTEM
                                   COPYRIGHT 2075-2077
   --------------------------------------------------------------------------------

   Username: _
   ```

### Login Credentials

Use one of the default accounts:

| Username | Password | Access Level |
|----------|----------|--------------|
| ADMIN    | ADMIN    | Full access  |
| GUEST    | GUEST    | Limited      |
| MAINT    | MAINT    | Maintenance  |

**Example:**
```
Username: ADMIN
Password: *****
```

## Serial Console Access

For debugging and development, connect via serial console:

### Hardware Connection

Connect USB-to-Serial adapter to GPIO pins:
- **GND** → Pin 6 (Ground)
- **TX** → Pin 10 (GPIO 15, RX)
- **RX** → Pin 8 (GPIO 14, TX)

### Serial Settings
- **Baud Rate:** 115200
- **Data Bits:** 8
- **Stop Bits:** 1
- **Parity:** None
- **Flow Control:** None

### Access via Terminal

#### Linux/macOS
```bash
# Find device
ls /dev/tty*

# Connect with screen
screen /dev/ttyUSB0 115200

# Or with minicom
minicom -D /dev/ttyUSB0 -b 115200
```

#### Windows
Use PuTTY or similar:
1. Select "Serial"
2. Set COM port (check Device Manager)
3. Speed: 115200
4. Connect

## Troubleshooting

### No Display Output

**Problem:** Black screen, no output

**Solutions:**
1. Check HDMI connection
2. Try different display
3. Add to config.txt:
   ```ini
   hdmi_safe=1
   hdmi_force_hotplug=1
   ```

### Stuck at Rainbow Screen

**Problem:** Raspberry Pi rainbow splash screen doesn't disappear

**Solutions:**
1. Check SD card files
2. Verify kernel.img is present
3. Check config.txt syntax
4. Try different SD card

### No Serial Output

**Problem:** No text on serial console

**Solutions:**
1. Check UART wiring
2. Verify enable_uart=1 in config.txt
3. Test with different terminal program
4. Check baud rate (must be 115200)

### Boot Loop

**Problem:** System keeps rebooting

**Solutions:**
1. Check kernel image size (<1MB)
2. Verify proper toolchain used
3. Test with serial console for error messages
4. Try rebuilding kernel

### Kernel Not Loading

**Problem:** RETROS-BIOS boots but UOS doesn't start

**Solutions:**
1. Verify MFBootAgent is installed
2. Check kernel image names match platform
3. Ensure all boot files present
4. Check SD card filesystem (must be FAT32)

### Display Corruption

**Problem:** Garbled text or artifacts

**Solutions:**
1. Adjust framebuffer settings in config.txt
2. Try different resolution
3. Disable overclocking if enabled
4. Check GPU memory allocation:
   ```ini
   gpu_mem=64
   ```

## Performance Tips

### Faster Boot
Add to config.txt:
```ini
boot_delay=0
disable_splash=1
```

### More RAM
Reduce GPU memory allocation:
```ini
gpu_mem=16
```

### Overclocking (at your own risk)
For Raspberry Pi 3:
```ini
arm_freq=1400
gpu_freq=500
over_voltage=2
```

## Updating UOS

To update to a newer version:

1. Build new kernel image
2. Backup SD card
3. Replace kernel8.img on SD card
4. Reboot

**Note:** Configuration and user data are not preserved yet.

## Multiple OS Setup

MFBootAgent supports multiple operating systems. To dual-boot:

1. Name kernels differently:
   - `uos_kernel.img`
   - `pipos_kernel.img`
   
2. MFBootAgent will detect and show boot menu

3. Use arrow keys to select OS

## Network Boot (Advanced)

UOS supports network boot via TFTP (future feature):

1. Configure DHCP server
2. Set up TFTP server
3. Add to config.txt:
   ```ini
   program_usb_boot_mode=1
   ```

## Backup and Recovery

### Backup SD Card

#### Linux/macOS
```bash
# Backup entire SD card
sudo dd if=/dev/sdX of=uos-backup.img bs=4M status=progress

# Compress
gzip uos-backup.img
```

#### Windows
Use Win32DiskImager or similar tool.

### Restore SD Card
```bash
# Linux/macOS
sudo dd if=uos-backup.img of=/dev/sdX bs=4M status=progress
```

## Next Steps

Once installed:
1. Log in with default credentials
2. Try basic commands: `HELP`, `DIRECTORY`, `SHOW`
3. Explore the maintenance mode exploit: `SET HALT RESTART/MAINT`
4. Read the User Guide in docs/user/

---

**RobCo Industries™** - *"The Future of Computing, Yesterday!"*
