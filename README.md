# UOS

UOS, the Unified Operating System, is the third stage of the RobCo terminal chain. It is a bare-metal AArch32 program for the Raspberry Pi, built on the shared [RobCo-Core](https://github.com/RobCo-Industries/RobCo-Core) library. The Pi firmware loads RETROS-BIOS, RETROS-BIOS loads MFBootAgent, and MFBootAgent loads `/ROBCO/UOS.SYS`. UOS also boots alone.

The goal is a small real system in the style of the terminals of the Fallout games: a login, a command shell, a menu screen, a password minigame, a holotape reader and device control through GPIO. All text and content are original.

## Status

- The kernel base boots in QEMU on raspi0, raspi1ap, raspi2b and raspi3b, alone and after earlier stages.
- It brings up the screen, the tick, the heap, the input layer and the card, reads `UOS.CFG` and shows the login.
- Eight pure modules are built and host-tested: the command language, file specifications, users and login, the on-screen keyboard, device control, the menu screen, the password minigame and holotapes. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#modules).
- Three services wire some of them into the running system: the file system, the console with its line reader and pager, and the devices on the GPIO pins. They work together and are reached through the maintenance port. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#services).
- A session service runs the login screen over them, with a lockout. After login the command shell runs, see [docs/COMMANDS.md](docs/COMMANDS.md), or the terminal menu for a user whose home holds `TERMINAL.CFG`. See [docs/USER_MANUAL.md](docs/USER_MANUAL.md#logging-in).
- The holotape reader, the password game and the terminal menu run as screens. The shell opens each with `RUN HOLOTAPE`, `RUN HACK` and `RUN TERMINAL`. The game plays only at the login prompt, so `RUN HACK` from a session shows why it cannot start. See [docs/USER_MANUAL.md](docs/USER_MANUAL.md#the-terminal-menu).
- `LOGIN <user>` asks only for that user's password. There is no mail.
- The default card holds a user database, sample devices, a menu for OPER, word lists and holotapes. Their formats are in [docs/FORMATS.md](docs/FORMATS.md).
- Nothing is validated on real hardware.

## Out of scope

- USB keyboards
- Networking
- Preemptive multitasking, processes and user mode

## Build

You need `arm-none-eabi-gcc`, GNU make, Python 3 and, for the tests, QEMU and mtools.

```sh
git submodule update --init
make            # build/UOS.SYS
make test       # host tests, then target tests on every board
make test-tools # pytest over the card tools
make sdimage    # build/sd.img with UOS.SYS and the card/ tree
make run        # boot build/sd.img in QEMU on this terminal
```

See [docs/BUILD.md](docs/BUILD.md), [docs/TESTING.md](docs/TESTING.md) and [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Licence

MIT. See [LICENSE](LICENSE).
