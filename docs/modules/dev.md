# Module dev: device control

`src/dev/` holds the pure parts of device control. `devcfg.c` reads `DEVICES.CFG`. `devstate.c` keeps each device's state, turns commands into pin operations and formats status lines. The GPIO glue applies the operations. Nothing here is validated on real hardware.

## File format

`/UOS/DEVICES.CFG` is INI text, read with the RobCo-Core INI reader. LF and CR LF line ends both work. Comments start with `;` or `#`.

```
[device DOOR1]
type = door
gpio = 12
active = high
initial = off
pulse_ms = 0
label = Workshop door
privs = 2
```

| Key | Values | Default |
|---|---|---|
| section | `[device NAME]`, NAME 1 to 8 of `A-Z 0-9 _`, any case, stored upper-case | required |
| `type` | `door`, `light`, `turret`, `lock`, `relay`, `sensor`, any case | required |
| `gpio` | `<n>`: a BCM pin number, decimal, not refused. `X<chip>.<pin>`: an expander pin, chip 0 to 3, pin 0 to 7. Not used by another device. | required |
| `active` | `high` or `low`: the pin level that means on | `high` |
| `initial` | `on` or `off`: the state set at boot. Ignored for a sensor. Must be `off` when `pulse_ms` is set | `off` |
| `pulse_ms` | 0 for a level device, or 10 to 10000 | 0 |
| `label` | up to 31 printable ASCII characters | empty |
| `privs` | 32-bit mask, decimal or `0x` hex: the privilege bits needed to change the device | 2 (`OPER`) |

Keys are lower-case. Rules:

- A device with a bad field is skipped. The others still load. Each skipped device adds one error with its code, line and name.
- Unknown keys are skipped and counted. Keys outside a `[device NAME]` section count as unknown.
- Only the first 24 device sections are read, `DV_MAX_DEVICES`. Later ones are ignored and counted without checks.
- Two devices may not share a name or a pin. The later one is skipped.
- A sensor with `pulse_ms` other than 0 is skipped.
- A `[device NAME]` or `[expander N]` section with no keys is skipped with `DV_ERR_EMPTY`, `%DEVICE-E-EMPTY`. The reader is `ini_parse_sections()`, which reports every header.
- A repeated header opens a new section, also right after itself: a repeated device is `DV_ERR_DUPNAME`, a repeated expander `DV_ERR_EXPANDER`. The first one wins.
- An error about the whole section, such as a bad name, a missing key or a duplicate, carries the line of the section header. An error about one key carries that key's line.
- If the INI reader stops on a bad line, devices finished before that line stay loaded. The device in progress is dropped.

- An output with `active = high` on a pin pulled up at reset loads with a warning. Each warning adds its code, line and name to `warns`, and `warn_count` counts them all.

`dv_cfg_summary()` formats the result for the `ev=config` marker: `loaded=4 skipped=0 unknown=0 ignored=0 errors=0 parse=0 warnings=0`.

## Expander pins

An `[expander N]` section declares I2C expander chip N, 0 to 3. Its one key, `addr`, is the chip's 7-bit address: 0x20 to 0x27 or 0x38 to 0x3F, decimal or `0x` hex. A device names an expander pin as `gpio = X<chip>.<pin>`, any case.

```
[expander 0]
addr = 0x20

[expander 1]
addr = 0x38

[device LAMP1]
type = light
gpio = X0.3
label = Store room lamp

[device GATE]
type = door
gpio = X1.0
pulse_ms = 500
```

Rules:

- A chip may be declared before or after its devices. A device on a chip with no good `[expander N]` section is skipped with `DV_ERR_NOCHIP` once the whole file is read.
- N other than one digit 0 to 3, or a chip declared twice, gives `DV_ERR_EXPANDER`. The first good section of a chip wins.
- A missing or bad `addr`, a second `addr` key, or an address another chip already has gives `DV_ERR_EXPADDR`. The chip is then not declared.
- Other keys in an expander section are unknown and counted.
- Expander errors are counted in `err_count` with the name `X` and N, for example `X1`. They do not count as skipped devices.
- Two devices may not share one expander pin. The same pin number on another chip, or on the Pi, is fine.
- Refused pins and the pull-up warning apply to Pi pins only.

Each device and each pin operation has a `chip` field: `DV_CHIP_PI` for a Pi pin, or 0 to 3. `dv_set_t.exp[chip]` holds the chip's address. The status line shows `X1.0` in place of `GPIO n`. The glue in `src/dev/devglue.c` routes expander operations to `exp_write()`, `exp_config()` and `exp_read()`. Until the library's expander driver is pinned, these refuse every operation with `DEVS_ERR_NOEXP`, `%DEVICE-E-NOEXPDRV`. The pin record ring keeps the pin number only.

## Refused pins

`dv_cfg_parse_pins()` takes the refused pin mask: bit n refuses GPIO n. `dv_cfg_parse()` uses `DV_REFUSED_DEFAULT`. The masks come from RobCo-Core `docs/spec/BOARD_SUPPORT.md`, section "Pins", and the input mapping it lists:

| Pins | Why | Mask |
|---|---|---|
| 0, 1 | ID EEPROM. The input example avoids them. | `DV_PINS_FIXED` |
| 2, 3 | I2C | `DV_PINS_FIXED` |
| 7 to 11 | SPI0 | `DV_PINS_FIXED` |
| 14, 15 | UART | `DV_PINS_FIXED` |
| 18 | PWM audio on the Zero. Refused on every board, so one card works on all of them. | `DV_PINS_FIXED` |
| 5, 6, 17, 22, 26, 27 | the six buttons: UP, DOWN, LEFT, RIGHT, SELECT, BACK | `DV_PINS_BUTTONS` |
| 16, 23, 24 | the dial: push, A, B | `DV_PINS_DIAL` |
| 28 and up | PWM audio on Pi 1 to 3 (40, 41, 45), SD routing (34 to 39, 48 to 53), pins not on the header | always |

`DV_REFUSED_DEFAULT` refuses all of these. It leaves 7 free pins: 4, 12, 13, 19, 20, 21 and 25. UOS has six buttons and no dial, so its glue passes `DV_REFUSED_UOS`, which frees the dial's pins. That leaves 10 free Pi pins: 4, 12, 13, 16, 19, 20, 21, 23, 24 and 25. So at most 10 devices can load on Pi pins. With expanders, up to `DV_MAX_DEVICES`, 24, load in all.

## Pins pulled up at reset

GPIO 0 to 8 have a pull-up at reset, before any code runs, and GPIO 9 to 27 a pull-down. This comes from the BCM2835 reset defaults. No file in this tree states the table, so it is to verify. An `active = high` output on a pulled-up pin is on from power up until boot sets its initial level: a relay board may switch its load on. The parser loads such a device and adds a `DV_WARN_PULLUP` warning, `%DEVICE-W-PULLUP, active-high output on a pin pulled up at reset`. `DV_PINS_RESET_PULLUP` holds the list. Of the free pins, only GPIO 4 is in it. The shipped `DEVICES.CFG` puts its active-high door on GPIO 12.

## Commands per type

| Type | On word | Off word | Commands |
|---|---|---|---|
| door | `OPEN` | `CLOSED` | `ON`, `OFF`, `TOGGLE`, `PULSE`, `OPEN`, `CLOSE` |
| light | `ON` | `OFF` | `ON`, `OFF`, `TOGGLE`, `PULSE` |
| turret | `ACTIVE` | `STANDBY` | `ON`, `OFF`, `TOGGLE`, `PULSE`, `ACTIVATE`, `STANDBY` |
| lock | `UNLOCKED` | `LOCKED` | `ON`, `OFF`, `TOGGLE`, `PULSE`, `UNLOCK`, `LOCK` |
| relay | `ON` | `OFF` | `ON`, `OFF`, `TOGGLE`, `PULSE` |
| sensor | `TRIPPED` | `CLEAR` | none: read-only |

`OPEN`, `UNLOCK` and `ACTIVATE` mean on. `CLOSE`, `LOCK` and `STANDBY` mean off. Command words are case-insensitive.

`dv_command()` checks in this order: a sensor refuses everything, a word must belong to the type, then privileges, then `PULSE` needs `pulse_ms`. A user with `SYSTEM` (bit 0) always passes. Others need every bit of the device's `privs`. A mask of 0 lets anyone change the device. A refused command changes nothing.

## Pulse rule

A device with `pulse_ms` above 0 pulses for every word that turns it on: `ON`, `PULSE`, the on word, and `TOGGLE` from off. The command returns two operations: the on level now, and the off level with `after_ms` set. The second one is informative. `dv_tick()` emits it when it is due.

- Ticks are milliseconds passed in by the caller. The end is due when `now - due` as a signed 32-bit value is 0 or more, so the counter may wrap.
- Every accepted command cancels a pending pulse end first. `OFF` turns the device off at once. `PULSE` or `ON` during a pulse restarts it. `TOGGLE` during a pulse turns it off.
- `PULSE` on a device with `pulse_ms = 0` is refused with `NOPULSE`.
- `dv_tick()` writes at most `max` operations, in table order. The rest stay pending for the next call.

## Boot

`dv_boot_ops()` gives one operation per device. An output gets `CONFIG_OUTPUT` at the level of its `initial` state, which maps to `gpio_config_output()`. A sensor gets `CONFIG_INPUT` with a pull toward its inactive level: pull down for `active = high`, pull up for `active = low`. That maps to `gpio_config_input()`. `DV_PULL_*` equals `GPIO_PULL_*`.

## Status lines

`SHOW DEVICES` uses `dv_status_header()` and `dv_status_line()`. Columns: name 8, type 6, label 31, state 8, then `GPIO n`. The longest line is 64 characters. A device in fault adds ` FAULT=<count>`, at most 12 more characters. An output unreached at boot adds ` UNREACHABLE`. `devs_status()` adds ` UNREACH=<count>` on `ARM`'s line while other outputs are unreachable. `DV_STATUS_LINE_MAX` is 96 to hold them all.

```
NAME     TYPE   LABEL                           STATE    PIN
DOOR1    DOOR   Workshop door                   CLOSED   GPIO 12
```

`dv_state_word()` gives the one word for a `DEV:NAME` read.

## Pin faults

The device service keeps a fault per device in `dv_dstate_t`: `fault`, the `owed` level, the `owed_kind` of operation and the `fails` count. The rule: after a failed write, the model never says a state the pin does not have. A failed write toward the safe level leaves the state as last written and owes the write. `devs_tick()` retries it every tick until it succeeds. A device in fault refuses commands with `DEVS_ERR_FAULT`. A failed write away from the safe level only returns its code. `unreached` marks an output whose boot write failed without a fault, such as `NOEXPDRV`. Any successful write to it clears the mark. `ARM` is switched on at boot only if every other output was written to its initial level. The rule for each write site is in `docs/ARCHITECTURE.md`, "Pin faults".

```
VAULT    LOCK   Vault bolt                      UNLOCKED GPIO 20 FAULT=3
```

## Messages

`dv_strerror()` gives one line per code, in the OpenVMS shape:

| Code | Text |
|---|---|
| `DV_ERR_NOSUCHDEV` | `%DEVICE-W-NOSUCHDEV, no such device` |
| `DV_ERR_NOPRIV` | `%DEVICE-W-NOPRIV, insufficient privilege` |
| `DV_ERR_READONLY` | `%DEVICE-W-READONLY, device is read-only` |
| `DV_ERR_BADACTION` | `%DEVICE-W-BADACTION, action not valid for this device` |
| `DV_ERR_NOPULSE` | `%DEVICE-W-NOPULSE, device has no pulse length` |

The configuration errors use the `E` severity, for example `%DEVICE-E-PINUSED, pin is reserved by the system`.

## Pin record

`dv_rec_t` keeps the last 64 pin writes as pin, level and tick. The glue adds each write. `dv_rec_format()` prints entry `i`, oldest first, as `pin=4 level=1 tick=1500` for the maintenance port.
