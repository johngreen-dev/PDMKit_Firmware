# PDMKit — App ↔ Device Protocol Reference

Complete reference for building the PDMKit App. Covers USB connection, the `RS_` command protocol, all data formats, rule types, NVS storage layout, and board pin mapping.

---

## Table of Contents

1. [USB Connection](#1-usb-connection)
2. [Protocol Fundamentals](#2-protocol-fundamentals)
3. [Setup Session Flow](#3-setup-session-flow)
4. [Command Reference](#4-command-reference)
   - 4.1 [Session](#41-session)
   - 4.2 [Board Pin Map](#42-board-pin-map)
   - 4.3 [Pin Management](#43-pin-management)
   - 4.4 [Rule Management](#44-rule-management)
   - 4.5 [Variable Management](#45-variable-management)
   - 4.6 [Group Management](#46-group-management)
   - 4.7 [I/O Control](#47-io-control)
   - 4.8 [Diagnostics](#48-diagnostics)
   - 4.9 [CAN Configuration](#49-can-configuration)
5. [Response Data Formats](#5-response-data-formats)
6. [Rule Type Reference](#6-rule-type-reference)
7. [NVS Storage Format](#7-nvs-storage-format)
8. [Board Pin Map](#8-board-pin-map)
9. [Signal Evaluation Engine](#9-signal-evaluation-engine)
10. [Error Codes](#10-error-codes)
11. [Recommended App Connection Flow](#11-recommended-app-connection-flow)

---

## 1. USB Connection

The device enumerates as a USB CDC-ACM (virtual serial port) using TinyUSB.

| Property        | Value                     |
|-----------------|---------------------------|
| Vendor ID       | `0x303A`                  |
| Product ID      | `0x1002`                  |
| Manufacturer    | `PDMKit`                  |
| Product name    | `PDMKit_Controller`       |
| Serial number   | `PDM-001`                 |
| Interface       | USB CDC-ACM               |
| Baud rate       | Any — CDC-ACM ignores it  |
| Line ending     | `\n` or `\r\n`            |
| Encoding        | UTF-8 / ASCII             |

**Auto-detection:** Scan available serial ports and look for VID `0x303A` + PID `0x1002`. On Windows the port will appear as a standard COM port.

**USB speed:** High-speed (USB 2.0 HS) on the ESP32-P4.

---

## 2. Protocol Fundamentals

- Every **command** is a single line of plain text terminated with `\n`.
- All commands **must start with `RS_`** — lines not starting with `RS_` are silently ignored by the firmware.
- Every command produces at least one **response line** ending with `\n`.
- Some commands produce a **data block**: a `*_BEGIN` marker, zero or more data lines, then a `*_END` marker.
- Responses beginning with `OK_` indicate success; `ERR_` indicates failure.
- Commands are processed serially — do not send a new command until the previous response is complete.

### Timing notes

- Response latency: < 5 ms for simple commands.
- Data blocks (ListPins, ListRules, etc.): completed within < 50 ms for typical configs.
- After `RS_SaveSetup` the device applies the new config immediately; no reboot is needed.

---

## 3. Setup Session Flow

Most configuration commands require the device to be in **setup mode**. Use the session commands to enter and exit it.

```
App                         Device
 │                              │
 ├── RS_StartSetup ──────────►  │  enters setup mode
 │  ◄──────────── OK_StartSetup │
 │                              │
 ├── RS_AddOutput LED1 IO_PIN_9 ►│  stage pin
 │  ◄──────────── OK_AddOutput  │
 │                              │
 ├── RS_AddRule direct SW1 LED1 ►│  stage rule
 │  ◄──────────── OK_AddRule    │
 │                              │
 ├── RS_SaveSetup ────────────► │  commit to NVS + apply
 │  ◄─────────── OK_SaveSetup   │
```

- **Changes are staged in RAM** during a setup session.
- `RS_SaveSetup` writes to NVS flash and immediately applies the new config (no reboot required).
- `RS_CancelSetup` discards staged changes and reverts to the last saved state.
- Setup mode persists across device resets (`setup_mode` NVS key). If `setup_mode = true` in NVS the device starts in setup mode — always call `RS_SaveSetup` or `RS_CancelSetup` to close a session properly.

---

## 4. Command Reference

### 4.1 Session

#### `RS_StartSetup`

Enter setup mode. Required before any pin/rule/var/group add/remove commands.

```
→ RS_StartSetup
← OK_StartSetup
```

#### `RS_SaveSetup`

Persist all staged changes to NVS flash, apply the new config, and exit setup mode.

```
→ RS_SaveSetup
← OK_SaveSetup
```

#### `RS_CancelSetup`

Discard all staged changes, reload last saved config, and exit setup mode.

```
→ RS_CancelSetup
← OK_CancelSetup
```

---

### 4.2 Board Pin Map

#### `RS_ListBoardPins`

Returns the hardcoded connector-label → GPIO mapping burned into the firmware. Use this to populate the pin-picker UI. Works outside setup mode.

```
→ RS_ListBoardPins
← BOARD_PINS_BEGIN
   IO_PIN_1:gpio2
   IO_PIN_9:gpio21
   IO_PIN_17:gpio46:adc1_ch5
   ADC_PIN_1:gpio46:adc1_ch5
   ...
   BOARD_PINS_END
```

Each line: `<label>:<gpio_info>[:<adc_info>]`

- `gpio_info` — `gpioN` where N is the GPIO number
- `adc_info` — `adcU_chC` where U = ADC unit (1 or 2), C = channel; only present on ADC-capable pins

Connector labels can be passed directly to `RS_AddOutput`, `RS_AddInput`, and `RS_AddPWM` instead of raw GPIO numbers.

---

### 4.3 Pin Management

> Requires setup mode. Changes are staged until `RS_SaveSetup`.

#### `RS_AddOutput <name> <gpio>`

Define a digital output. `<gpio>` may be a connector label (`IO_PIN_9`) or a raw GPIO number (`21`).

```
→ RS_AddOutput LED1 IO_PIN_9
← OK_AddOutput

→ RS_AddOutput LED1 21
← OK_AddOutput
```

Adding a pin with an existing name replaces it.

#### `RS_AddInput <name> <gpio> [up|down|none]`

Define a digital input. Pull mode defaults to `none`.

```
→ RS_AddInput SW1 IO_PIN_10 up
← OK_AddInput
```

#### `RS_AddADC <name> <board_pin_label>`  
#### `RS_AddADC <name> <unit> <channel>`

Define an ADC input. Two forms:

```
→ RS_AddADC VBAT ADC_PIN_3          ← board label form (preferred)
← OK_AddADC

→ RS_AddADC VBAT 1 7                ← explicit unit + channel
← OK_AddADC
```

ADC-capable board labels: `IO_PIN_17..21`, `ADC_PIN_1..5` (see [Board Pin Map](#8-board-pin-map)).

#### `RS_AddPWM <name> <gpio> [freq_hz]`

Define a PWM output. Frequency defaults to 5000 Hz.

```
→ RS_AddPWM FAN IO_PIN_5 1000
← OK_AddPWM
```

#### `RS_RemovePin <name>`

Remove a pin from the staged config.

```
→ RS_RemovePin LED1
← OK_RemovePin      (or ERR_NOT_FOUND)
```

#### `RS_ListPins`

List all currently configured pins. Works outside setup mode (shows last applied config).

```
→ RS_ListPins
← PINS_BEGIN
   LED1:dout:gpio21
   SW1:din:gpio22:up
   VBAT:adc:u1c7
   FAN:pwm:gpio6:1000Hz
   PINS_END
```

Line formats per type:

| Type   | Format                           |
|--------|----------------------------------|
| Output | `<name>:dout:gpio<N>`            |
| Input  | `<name>:din:gpio<N>:<pull>`      |
| ADC    | `<name>:adc:u<U>c<C>`           |
| PWM    | `<name>:pwm:gpio<N>:<freq>Hz`   |

---

### 4.4 Rule Management

> Requires setup mode.

#### `RS_AddRule <type> <args…>`

Add a logic rule. See [Rule Type Reference](#6-rule-type-reference) for all types and argument formats.

```
→ RS_AddRule direct SW1 LED1
← OK_AddRule

→ RS_AddRule expr ALL_LIGHTS SW1 AND NOT SW2
← OK_AddRule
```

#### `RS_RemoveRule <index>`

Remove a rule by its zero-based index (from `RS_ListRules`).

```
→ RS_RemoveRule 2
← OK_RemoveRule    (or ERR_NOT_FOUND)
```

#### `RS_ListRules`

List all rules. Works outside setup mode.

```
→ RS_ListRules
← RULES_BEGIN
   0:direct:SW1->LED1
   1:expr:SW1 AND NOT SW2->ALL_LIGHTS
   2:flasher:->LED2
   RULES_END
```

Line format: `<index>:<type>:<src>-><dst>`

- For EXPR rules, `<src>` is the full expression text.
- For oscillator/no-source rules, `<src>` is empty.

---

### 4.5 Variable Management

Variables are named boolean expressions evaluated before rules each tick. They can be referenced as inputs in other expressions and rules.

> Requires setup mode.

#### `RS_AddVar <name> <expression>`

Define or replace a named boolean variable.

```
→ RS_AddVar BOTH_ON SW1 AND SW2
← OK_AddVar
```

#### `RS_RemoveVar <name>`

```
→ RS_RemoveVar BOTH_ON
← OK_RemoveVar    (or ERR_NOT_FOUND)
```

#### `RS_ListVars`

```
→ RS_ListVars
← VARS_BEGIN
   BOTH_ON:SW1 AND SW2
   VARS_END
```

Line format: `<name>:<expression>`

---

### 4.6 Group Management

Groups are named collections of output pins. A group can be used as a **destination** (drives all member pins at once) or as an **input** (true if any member is currently on).

> Requires setup mode.

#### `RS_AddGroup <name> <member1> [member2…]`

```
→ RS_AddGroup ALL_LIGHTS LED1 LED2 LED3
← OK_AddGroup
```

#### `RS_RemoveGroup <name>`

```
→ RS_RemoveGroup ALL_LIGHTS
← OK_RemoveGroup    (or ERR_NOT_FOUND)
```

#### `RS_ListGroups`

```
→ RS_ListGroups
← GROUPS_BEGIN
   ALL_LIGHTS:LED1,LED2,LED3
   GROUPS_END
```

Line format: `<name>:<member1>,<member2>,…`

---

### 4.7 I/O Control

> Works outside setup mode. Operates on the **currently registered** config (from last applied save). Does not affect staged changes.

#### `RS_SetOutput <name> <0|1>`

Drive a digital output high (`1`) or low (`0`).

```
→ RS_SetOutput LED1 1
← OK_SetOutput    (or ERR_PIN_NOT_FOUND)
```

#### `RS_GetInput <name>`

Read a digital input.

```
→ RS_GetInput SW1
← INPUT SW1:1
```

Response format: `INPUT <name>:<value>` where value is `0` or `1`.

---

### 4.8 Diagnostics

#### `RS_GetStorage`

Read all entries in the `storage` NVS namespace (pins, rules, and runtime settings).

```
→ RS_GetStorage
← STORAGE_BEGIN
   io_config:str:{"pins":[…],"rules":[…]}
   setup_mode:bool:true
   can_baud:int:500
   STORAGE_END
```

Line format: `<key>:<type>:<value>`

| Type      | Value format                          |
|-----------|---------------------------------------|
| `int`     | Decimal integer                       |
| `bool`    | `true` or `false`                     |
| `str`     | Raw string (may contain colons)       |
| `unknown` | `-`                                   |

---

### 4.9 CAN Configuration

#### `RS_SetCANBaud <kbps>`

Set the CAN bus baud rate and restart the TWAI driver immediately. Persisted to NVS — survives reboots. Works outside setup mode.

Valid values: `125`, `250`, `500`, `800`, `1000`

```
→ RS_SetCANBaud 250
← OK_SetCANBaud 250    (or ERR_BAD_ARGS for invalid value)
```

Default baud rate (if never set): **500 kbps**.

---

## 5. Response Data Formats

### `RS_ListBoardPins` block

```
BOARD_PINS_BEGIN
IO_PIN_1:gpio2
IO_PIN_2:gpio3
IO_PIN_3:gpio4
IO_PIN_4:gpio5
IO_PIN_5:gpio6
IO_PIN_6:gpio7
IO_PIN_7:gpio8
IO_PIN_8:gpio20
IO_PIN_9:gpio21
IO_PIN_10:gpio22
IO_PIN_11:gpio23
IO_PIN_12:gpio26
IO_PIN_13:gpio27
IO_PIN_14:gpio32
IO_PIN_15:gpio33
IO_PIN_16:gpio36
IO_PIN_17:gpio46:adc1_ch5
IO_PIN_18:gpio47:adc1_ch6
IO_PIN_19:gpio48:adc1_ch7
IO_PIN_20:gpio53:adc2_ch4
IO_PIN_21:gpio54:adc2_ch5
ADC_PIN_1:gpio46:adc1_ch5
ADC_PIN_2:gpio47:adc1_ch6
ADC_PIN_3:gpio48:adc1_ch7
ADC_PIN_4:gpio53:adc2_ch4
ADC_PIN_5:gpio54:adc2_ch5
BOARD_PINS_END
```

> **Note:** `IO_PIN_3` (GPIO4) is the CAN CTX line and `IO_PIN_4` (GPIO5) is CAN CRX. Avoid using them as general GPIO when CAN is active.

### `RS_ListPins` block

```
PINS_BEGIN
<name>:dout:gpio<N>
<name>:din:gpio<N>:<pull>
<name>:adc:u<U>c<C>
<name>:pwm:gpio<N>:<freq>Hz
PINS_END
```

- `<pull>` — `up`, `down`, or `none`
- `<U>` — ADC unit: `1` or `2`
- `<C>` — ADC channel number

### `RS_ListRules` block

```
RULES_BEGIN
<idx>:<type>:<src>-><dst>
RULES_END
```

- `<idx>` — zero-based integer index (used for `RS_RemoveRule`)
- `<src>` — source pin name, expression text, or empty for oscillator types
- `<dst>` — destination pin or group name

### `RS_ListVars` block

```
VARS_BEGIN
<name>:<expression>
VARS_END
```

### `RS_ListGroups` block

```
GROUPS_BEGIN
<name>:<member1>,<member2>,…
GROUPS_END
```

### `RS_GetStorage` block

```
STORAGE_BEGIN
<key>:<type>:<value>
STORAGE_END
```

---

## 6. Rule Type Reference

All rules are added with `RS_AddRule <type> <args…>`.

CAN IDs may be decimal (`512`) or `0x`-prefixed hex (`0x200`).

---

### Expression

| Type   | Wire format                                     |
|--------|-------------------------------------------------|
| `expr` | `expr <dst_or_group> <boolean expression text>` |

```
RS_AddRule expr LED1 SW1 AND NOT SW2
RS_AddRule expr ALL_LIGHTS (SW1 OR SW2) AND ENABLED
```

Expression operators: `AND`, `OR`, `NOT` (case-insensitive), parentheses. Operands are pin names, variable names, or group names.

---

### Combinational

| Type       | Wire format                                            | Notes                             |
|------------|--------------------------------------------------------|-----------------------------------|
| `direct`   | `direct <src> <dst> [invert:0|1]`                     | Pass-through, optional invert     |
| `not`      | `not <src> <dst>`                                     | Logical NOT                       |
| `and`      | `and <src1> <dst>`                                    | All srcs high → dst high ¹        |
| `or`       | `or <src1> <dst>`                                     | Any src high → dst high ¹         |
| `nand_nor` | `nand_nor <src1> <dst> [nor_mode:0|1]`                | 0=NAND (default), 1=NOR           |
| `xor`      | `xor <src1>/<src2> <dst>`                             | src1 and src2 separated by `/`    |

¹ Multi-source rules: the firmware stores only the first source via the RS_ command. Use `expr` for full multi-input logic.

---

### Timing

| Type        | Wire format                                          |
|-------------|------------------------------------------------------|
| `on_delay`  | `on_delay <src> <dst> <delay_ms>`                    |
| `off_delay` | `off_delay <src> <dst> <delay_ms>`                   |
| `min_on`    | `min_on <src> <dst> <on_ms>`                         |
| `one_shot`  | `one_shot <src> <dst> <pulse_ms>`                    |
| `pulse_str` | `pulse_str <src> <dst> <stretch_ms>`                 |
| `debounce`  | `debounce <src> <dst> <stable_ms>`                   |

---

### Oscillator

| Type      | Wire format                                                         |
|-----------|---------------------------------------------------------------------|
| `flasher` | `flasher <dst> <on_ms> <off_ms>`                                    |
| `hazard`  | `hazard <dst> <on_ms> <off_ms>`                                     |
| `burst`   | `burst <dst> <pulse_count> <interburst_gap_ms>`                     |
| `pwm_out` | `pwm_out <dst> <freq_hz> <duty_pct>`                                |

---

### Threshold / ADC

| Type         | Wire format                                                                |
|--------------|----------------------------------------------------------------------------|
| `threshold`  | `threshold <src_adc> <dst> <threshold_mv> [invert:0|1]`                   |
| `hysteresis` | `hysteresis <src_adc> <dst> <lo_mv> <hi_mv>`                              |
| `window`     | `window <src_adc> <dst> <lo_mv> <hi_mv>`                                  |
| `adc_map`    | `adc_map <src_adc> <dst_pwm> <in_lo_mv> <in_hi_mv> [out_lo_%] [out_hi_%]`|

---

### Stateful

| Type        | Wire format                                                     |
|-------------|-----------------------------------------------------------------|
| `sr_latch`  | `sr_latch <set_pin>/<reset_pin> <dst>`                         |
| `toggle`    | `toggle <src> <dst>`                                           |
| `interlock` | `interlock <src1> <dst>`  ¹                                    |
| `prio_or`   | `prio_or <src1> <dst>`    ¹                                    |
| `n_press`   | `n_press <src> <dst> <count> <window_ms>`                      |

¹ Use `expr` for full multi-source interlock/priority logic.

---

### Protective

| Type        | Wire format                                                           |
|-------------|-----------------------------------------------------------------------|
| `oc_latch`  | `oc_latch <src_adc> <dst> <threshold_mv> <confirm_ms>`               |
| `retry`     | `retry <fault_src> <dst> <max_retries> <backoff_ms>`                  |
| `therm_drt` | `therm_drt <src_adc> <dst_pwm> <derate_start_mv> <max_mv>`           |
| `watchdog`  | `watchdog <src> <dst> <timeout_ms>`                                   |

---

### CAN — Receive (RX)

| Type          | Wire format                                                                                  | Status      |
|---------------|----------------------------------------------------------------------------------------------|-------------|
| `can_sig`     | `can_sig <id> <byte_off> <bit_off> <bit_len> <dst> [threshold] [invert:0|1]`                | Implemented |
| `can_thr`     | `can_thr <id> <byte_off> <bit_off> <bit_len> <dst> <lo> <hi>`                               | Implemented |
| `can_map`     | `can_map <id> <byte_off> <bit_off> <bit_len> <dst_pwm> <in_lo> <in_hi> [out_lo%] [out_hi%]`| Implemented |
| `can_timeout` | `can_timeout <id> <dst> <window_ms> [invert:0|1]`                                           | Implemented |
| `can_hrx`     | `can_hrx <id> <dst> <window_ms>`                                                             | Implemented |
| `can_cmd_out` | `can_cmd_out <id> <byte_off> <cmd_value> <dst>`                                             | Implemented |

CAN signal bit extraction uses **Intel (LSB-first)** byte order. `byte_off` is 0–7, `bit_off` is 0–7 within that byte.

---

### CAN — Transmit (TX)

| Type       | Wire format                                                     | Status      |
|------------|-----------------------------------------------------------------|-------------|
| `can_tx_st`| `can_tx_st <src_gpio> <id> <interval_ms>`                       | Implemented |
| `can_tx_an`| `can_tx_an <src_adc> <id> <byte_off> <interval_ms>`             | Implemented |
| `can_htx`  | `can_htx <id> <interval_ms>`                                    | Implemented |
| `can_boff` | `can_boff <dst>`                                                | Implemented |

---

## 7. NVS Storage Format

Configuration is stored under NVS namespace `storage`, key `io_config`, as a JSON string.

### Schema

```json
{
  "pins": [
    { "n": "LED1",  "t": "dout", "g": 21 },
    { "n": "SW1",   "t": "din",  "g": 22, "p": "up" },
    { "n": "VBAT",  "t": "adc",  "u": 1,  "c": 7 },
    { "n": "FAN",   "t": "pwm",  "g": 6,  "f": 1000 }
  ],
  "rules": [
    { "t": "direct", "src": "SW1", "dst": "LED1" },
    { "t": "expr",   "dst": "ALL_LIGHTS", "expr": "SW1 AND NOT SW2" },
    { "t": "flasher","dst": "BLINK", "on": 500, "off": 500 },
    { "t": "can_sig","dst": "RPM_HI", "cid": 512, "cby": 0, "cbi": 0, "cln": 16, "tlo": 3000 }
  ],
  "vars": [
    { "name": "BOTH_ON", "expr": "SW1 AND SW2" }
  ],
  "groups": [
    { "name": "ALL_LIGHTS", "members": ["LED1", "LED2", "LED3"] }
  ]
}
```

### Pin JSON fields

| Field | Type   | Meaning                              | Types that use it    |
|-------|--------|--------------------------------------|----------------------|
| `n`   | string | Pin name                             | All                  |
| `t`   | string | `dout` / `din` / `adc` / `pwm`      | All                  |
| `g`   | int    | GPIO number                          | dout, din, pwm       |
| `p`   | string | Pull: `up` / `down` / `none`         | din                  |
| `u`   | int    | ADC unit (1 or 2)                    | adc                  |
| `c`   | int    | ADC channel                          | adc                  |
| `f`   | int    | PWM frequency in Hz                  | pwm                  |

### Rule JSON fields

| Field    | Type    | Meaning                                         |
|----------|---------|-------------------------------------------------|
| `t`      | string  | Rule type wire name (e.g. `"direct"`, `"expr"`) |
| `src`    | string  | Single source pin name                          |
| `srcs`   | array   | Multiple source pin names                       |
| `src2`   | string  | Secondary source (SR_LATCH reset)               |
| `dst`    | string  | Destination pin or group name                   |
| `expr`   | string  | Boolean expression text (EXPR type)             |
| `on`     | int     | On time ms                                      |
| `off`    | int     | Off time ms                                     |
| `delay`  | int     | Delay ms                                        |
| `window` | int     | Window ms                                       |
| `pa`     | int     | Param A (type-specific)                         |
| `pb`     | int     | Param B (type-specific)                         |
| `tlo`    | int     | Low threshold (mv or raw)                       |
| `thi`    | int     | High threshold (mv or raw)                      |
| `olo`    | int     | Output low value                                |
| `ohi`    | int     | Output high value (default 100)                 |
| `inv`    | bool    | Invert output                                   |
| `cid`    | int     | CAN message ID                                  |
| `cby`    | int     | CAN byte offset (0–7)                           |
| `cbi`    | int     | CAN bit offset (0–7, LSB-first)                 |
| `cln`    | int     | CAN signal bit length (default 8)               |
| `cxt`    | bool    | CAN extended 29-bit ID                          |

### Other NVS keys (same namespace)

| Key          | Type  | Default | Meaning                              |
|--------------|-------|---------|--------------------------------------|
| `setup_mode` | bool  | false   | Persisted setup mode flag            |
| `can_baud`   | int   | 500     | CAN baud rate in kbps (set by `RS_SetCANBaud`) |

---

## 8. Board Pin Map

The following connector labels are hardcoded in `main/helper/board_pins.hpp`. They can be used anywhere a GPIO number is accepted in RS_ commands.

| Label       | GPIO | ADC Unit | ADC Ch | Notes                    |
|-------------|------|----------|--------|--------------------------|
| `IO_PIN_1`  |   2  |   —      |   —    |                          |
| `IO_PIN_2`  |   3  |   —      |   —    |                          |
| `IO_PIN_3`  |   4  |   —      |   —    | **CAN CTX** (TWAI TX)    |
| `IO_PIN_4`  |   5  |   —      |   —    | **CAN CRX** (TWAI RX)    |
| `IO_PIN_5`  |   6  |   —      |   —    |                          |
| `IO_PIN_6`  |   7  |   —      |   —    |                          |
| `IO_PIN_7`  |   8  |   —      |   —    |                          |
| `IO_PIN_8`  |  20  |   —      |   —    |                          |
| `IO_PIN_9`  |  21  |   —      |   —    |                          |
| `IO_PIN_10` |  22  |   —      |   —    |                          |
| `IO_PIN_11` |  23  |   —      |   —    |                          |
| `IO_PIN_12` |  26  |   —      |   —    |                          |
| `IO_PIN_13` |  27  |   —      |   —    |                          |
| `IO_PIN_14` |  32  |   —      |   —    |                          |
| `IO_PIN_15` |  33  |   —      |   —    |                          |
| `IO_PIN_16` |  36  |   —      |   —    |                          |
| `IO_PIN_17` |  46  |   1      |   5    | J1 pin 36                |
| `IO_PIN_18` |  47  |   1      |   6    | J1 pin 37                |
| `IO_PIN_19` |  48  |   1      |   7    | J1 pin 33                |
| `IO_PIN_20` |  53  |   2      |   4    | J1 pin 35                |
| `IO_PIN_21` |  54  |   2      |   5    | J1 pin 32                |
| `ADC_PIN_1` |  46  |   1      |   5    | Alias for IO_PIN_17      |
| `ADC_PIN_2` |  47  |   1      |   6    | Alias for IO_PIN_18      |
| `ADC_PIN_3` |  48  |   1      |   7    | Alias for IO_PIN_19      |
| `ADC_PIN_4` |  53  |   2      |   4    | Alias for IO_PIN_20      |
| `ADC_PIN_5` |  54  |   2      |   5    | Alias for IO_PIN_21      |

`resolveGpio()` in the firmware also accepts raw decimal (`21`) or hex (`0x15`) numbers for backwards compatibility.

---

## 9. Signal Evaluation Engine

Understanding how the firmware evaluates rules helps design correct logic configurations.

### Evaluation order per tick (1 ms)

```
1. Read physical pins → sig map
      OUTPUT pins:  sig["LED1"] = current GPIO level (feedback)
      INPUT pins:   sig["SW1"]  = current GPIO level

2. Compute group states → sig map
      For each group, OR all member values from sig
      sig["ALL_LIGHTS"] = sig["LED1"] || sig["LED2"] || sig["LED3"]
      → Groups can now be used as inputs in step 3 and 4

3. Evaluate variables → sig map
      sig["BOTH_ON"] = evalExpr("SW1 AND SW2", sig)
      → Variables can reference pins, groups, and other variables

4. Evaluate all rules → claims map
      Each rule reads from sig and writes a claim for its dst

5. Expand group destinations → individual pin claims
      Rule dst "ALL_LIGHTS" → claims["LED1"] |= val
                             → claims["LED2"] |= val
                             → claims["LED3"] |= val

6. Apply claims → sig + GPIO
      For each claimed pin: set GPIO level and update sig
```

### Key behaviours

- **Multiple rules on the same destination** are OR'd together — any rule claiming high wins.
- **Groups as destinations** expand to all member pins simultaneously.
- **Groups as inputs** read as `true` if ANY member is currently high (step 2).
- **Variables** are evaluated in declaration order; later variables can reference earlier ones.
- **CAN RX rules** source their values from the CAN frame store, not from `sig`.
- **Side-effect rules** (ADC_MAP, THERM_DRT, PWM_OUT, CAN TX types) return `false` to the claims map and are excluded from GPIO driving — they only write to PWM/CAN.

### Expression syntax

```
expr := term (OR term)*
term := factor (AND factor)*
factor := NOT factor | atom
atom := IDENT | '(' expr ')'
```

Identifiers are **case-sensitive** and must match pin names, variable names, or group names exactly.

---

## 10. Error Codes

| Response            | Meaning                                                    |
|---------------------|------------------------------------------------------------|
| `ERR_UNKNOWN_CMD`   | Command starts with `RS_` but is not recognised            |
| `ERR_NOT_IN_SETUP`  | Command requires setup mode — send `RS_StartSetup` first   |
| `ERR_BAD_ARGS`      | Missing, malformed, or out-of-range argument               |
| `ERR_NOT_FOUND`     | Named item does not exist in staged config                 |
| `ERR_PIN_NOT_FOUND` | Named pin is not in the active PinRegistry (applied config)|
| `ERR_BAD_EXPR`      | Boolean expression failed to parse; error text follows     |

`ERR_BAD_EXPR` response format: `ERR_BAD_EXPR <reason text>`

---

## 11. Recommended App Connection Flow

```
1.  Scan serial ports
    → Look for VID=0x303A PID=0x1002 (star-mark in UI)

2.  Open port at any baud (115200 suggested for logging tools)

3.  Query current state (no setup mode needed)
    → RS_ListBoardPins   (populate pin-picker with connector labels)
    → RS_ListPins        (show configured pins)
    → RS_ListRules       (show logic rules)
    → RS_ListVars        (show variables)
    → RS_ListGroups      (show groups)
    → RS_GetStorage      (show full NVS state for debug panel)

4.  Begin edit session
    → RS_StartSetup
    ← OK_StartSetup

5.  Make changes
    → RS_AddOutput / RS_AddInput / RS_AddADC / RS_AddPWM
    → RS_AddRule / RS_RemoveRule
    → RS_AddVar  / RS_RemoveVar
    → RS_AddGroup / RS_RemoveGroup

6.  Commit or discard
    → RS_SaveSetup     (write to NVS, apply immediately)
    → RS_CancelSetup   (discard, revert to last save)

7.  Refresh UI
    → RS_ListPins / RS_ListRules / RS_ListVars / RS_ListGroups

8.  Test
    → RS_SetOutput <name> <0|1>
    → RS_GetInput  <name>

9.  CAN baud rate (any time)
    → RS_SetCANBaud 250
```

### Parsing data blocks

Data blocks arrive as complete lines. Buffer lines between `*_BEGIN` and `*_END` markers, then process the batch when `*_END` arrives. Do not interleave other commands while a block is in progress.

```python
if line == "PINS_BEGIN":
    pending = []
elif line == "PINS_END":
    process_pins(pending)
elif collecting:
    pending.append(line)
```

### After `RS_SaveSetup`

The config is applied immediately. Re-query `RS_ListPins` / `RS_ListRules` to confirm the applied state matches what was staged.
