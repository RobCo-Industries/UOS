# Module dev: device control

`src/dev/` holds the pure parts of device control. `devcfg.c` reads `DEVICES.CFG`. `devstate.c` keeps each device's state, turns commands into pin operations and formats status lines. The GPIO glue applies the operations. Nothing here is validated on real hardware.

## File format

`/UOS/DEVICES.CFG` is INI text, read with the RobCo-Core INI reader. LF and CR LF line ends both work. Comments start with `;` or `#`.

```
[device DOOR1]
type = door
gpio = 4
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
| `gpio` | BCM pin number, decimal, not refused, not used by another device | required |
| `active` | `high` or `low`: the pin level that means on | `high` |
| `initial` | `on` or `off`: the state set at boot. Ignored for a sensor. Must be `off` when `pulse_ms` is set | `off` |
| `pulse_ms` | 0 for a level device, or 10 to 10000 | 0 |
| `label` | up to 31 printable ASCII characters | empty |
| `privs` | 32-bit mask, decimal or `0x` hex: the privilege bits needed to change the device | 2 (`OPER`) |

Keys are lower-case. Rules:

- A device with a bad field is skipped. The others still load. Each skipped device adds one error with its code, line and name.
- Unknown keys are skipped and counted. Keys outside a `[device NAME]` section count as unknown.
- Only the first 16 device sections are read. Later ones are ignored and counted without checks.
- Two devices may not share a name or a pin. The later one is skipped.
- A sensor with `pulse_ms` other than 0 is skipped.
- A section with no keys is never seen by the reader, so it is not counted.
- Two sections with the same name in a row merge into one, because the reader reports keys only.
- If the INI reader stops on a bad line, devices finished before that line stay loaded. The device in progress is dropped.

`dv_cfg_summary()` formats the result for the `ev=config` marker: `loaded=4 skipped=0 unknown=0 ignored=0 errors=0 parse=0`.

## Refused pins

From RobCo-Core `docs/spec/BOARD_SUPPORT.md`, section "Pins":

| Pins | Why |
|---|---|
| 0, 1 | ID EEPROM. The input example avoids them. |
| 2, 3 | I2C |
| 5, 6, 16, 17, 22, 23, 24, 26, 27 | input buttons and dial |
| 7 to 11 | SPI0 |
| 14, 15 | UART |
| 18 | PWM audio on the Zero |
| 28 and up | PWM audio on Pi 1 to 3 (40, 41, 45), SD routing (34 to 39, 48 to 53), pins not on the header |

The free pins are 4, 12, 13, 19, 20, 21 and 25. So at most 7 devices can load at once.

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

`SHOW DEVICES` uses `dv_status_header()` and `dv_status_line()`. Columns: name 8, type 6, label 31, state 8, then `GPIO n`. The longest line is 64 characters.

```
NAME     TYPE   LABEL                           STATE    PIN
DOOR1    DOOR   Workshop door                   CLOSED   GPIO 4
```

`dv_state_word()` gives the one word for a `DEV:NAME` read.

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
