# PDMKit Remote Setup API

Commands are sent as plain text over USB CDC (the device enumerates as **PDMKit_Controller**).
Each command is a single line terminated with `\n`. Responses are returned the same way.

---

## Transport

| Property     | Value                        |
|--------------|------------------------------|
| Interface    | USB CDC (virtual serial port) |
| Product name | `PDMKit_Controller`          |
| Baud rate    | Any (USB CDC ignores baud)   |
| Line ending  | `\n` or `\r\n`               |
| Encoding     | ASCII                        |

---

## Session commands

### `RS_StartSetup`

Enters remote setup mode. The device accepts I/O configuration commands until
`RS_SaveSetup` or `RS_CancelSetup` is received.

```
→ RS_StartSetup
← OK_StartSetup
```

---

### `RS_SaveSetup`

Persists all staged I/O pin changes to flash and exits setup mode.
The new configuration takes effect on the next boot.

```
→ RS_SaveSetup
← OK_SaveSetup
```

---

### `RS_CancelSetup`

Discards all staged I/O changes, reverts the in-memory config to the last saved
state, and exits setup mode.

```
→ RS_CancelSetup
← OK_CancelSetup
```

---

## Diagnostic commands

### `RS_GetStorage`

Reads all entries in the `storage` NVS namespace.

```
→ RS_GetStorage
← STORAGE_BEGIN
   <key>:<type>:<value>
   ...
   STORAGE_END
```

Types: `int`, `bool`, `str`, `unknown`.

---

## I/O configuration commands

> These commands require an active setup session (`RS_StartSetup` first).
> Sending them outside setup mode returns `ERR_NOT_IN_SETUP`.
> Changes are staged in memory and only written to flash by `RS_SaveSetup`.

### `RS_AddOutput <name> <gpio>`

Defines a digital output pin. If a pin with the same name already exists it is replaced.

```
→ RS_AddOutput LED1 20
← OK_AddOutput
```

---

### `RS_AddInput <name> <gpio> [up|down|none]`

Defines a digital input pin. Pull mode defaults to `none`.

```
→ RS_AddInput SW1 22 up
← OK_AddInput
```

---

### `RS_AddADC <name> <unit> <channel>`

Defines an ADC input by ADC unit (1 or 2) and channel number.

```
→ RS_AddADC VBAT 1 6
← OK_AddADC
```

---

### `RS_AddPWM <name> <gpio> [freq_hz]`

Defines a PWM output. Frequency defaults to 5000 Hz.

```
→ RS_AddPWM FAN 5 1000
← OK_AddPWM
```

---

### `RS_RemovePin <name>`

Removes a pin definition from the staged config.

```
→ RS_RemovePin LED1
← OK_RemovePin
```

Returns `ERR_NOT_FOUND` if the name does not exist.

---

## I/O query and control commands

> These commands work at any time (no setup mode required).
> They operate on pins that are **currently registered** (i.e. from the last saved
> config applied at boot) — not on unsaved staged changes.

### `RS_ListPins`

Lists all configured pins.

```
→ RS_ListPins
← PINS_BEGIN
   LED1:dout:gpio20
   SW1:din:gpio22:up
   VBAT:adc:u1c6
   FAN:pwm:gpio5:1000Hz
   PINS_END
```

---

### `RS_SetOutput <name> <0|1>`

Sets a digital output high (1) or low (0).

```
→ RS_SetOutput LED1 1
← OK_SetOutput
```

Returns `ERR_PIN_NOT_FOUND` if the pin is not registered.

---

### `RS_GetInput <name>`

Reads a digital input.

```
→ RS_GetInput SW1
← INPUT SW1:0
```

Returns `ERR_PIN_NOT_FOUND` if the pin is not registered.

---

## Error responses

| Response            | Meaning                                      |
|---------------------|----------------------------------------------|
| `ERR_UNKNOWN_CMD`   | Command starts with `RS_` but is unrecognised |
| `ERR_NOT_IN_SETUP`  | Command requires setup mode                  |
| `ERR_BAD_ARGS`      | Missing or malformed arguments               |
| `ERR_NOT_FOUND`     | Named pin does not exist in staged config    |
| `ERR_PIN_NOT_FOUND` | Named pin is not registered in PinRegistry   |

Lines that do not start with `RS_` are silently ignored.

---

## Example session — configure two pins and verify

```
→ RS_StartSetup
← OK_StartSetup

→ RS_AddOutput LED1 20
← OK_AddOutput

→ RS_AddInput SW1 22 up
← OK_AddInput

→ RS_SaveSetup
← OK_SaveSetup

  (reboot device)

→ RS_ListPins
← PINS_BEGIN
   LED1:dout:gpio20
   SW1:din:gpio22:up
   PINS_END

→ RS_SetOutput LED1 1
← OK_SetOutput

→ RS_GetInput SW1
← INPUT SW1:1
```

---

## Rule commands

> Rule commands require an active setup session (`RS_StartSetup` first).
> Rules are evaluated every tick by the main controller after the next boot.

### `RS_AddRule link <src> <dst>`

While `src` is HIGH, drives `dst` HIGH. Releases when `src` goes LOW.

```
→ RS_AddRule link SW1 LED1
← OK_AddRule
```

---

### `RS_AddRule toggle <src> <dst>`

Each rising edge on `src` flips the state of `dst`.

```
→ RS_AddRule toggle SW1 LED1
← OK_AddRule
```

---

### `RS_AddRule adc_pwm <src> <dst>`

Maps the ADC millivolt reading on `src` (0–3300 mV) proportionally to the PWM
duty cycle on `dst` (0.0–1.0).

```
→ RS_AddRule adc_pwm POT FAN
← OK_AddRule
```

---

### `RS_AddRule flash <dst> <on_ms> <off_ms>`

Blinks `dst` independently — `on_ms` high, `off_ms` low. No source pin needed.

```
→ RS_AddRule flash LED1 500 500
← OK_AddRule
```

---

### `RS_RemoveRule <index>`

Removes the rule at zero-based index (as shown by `RS_ListRules`).

```
→ RS_RemoveRule 0
← OK_RemoveRule
```

Returns `ERR_NOT_FOUND` if the index is out of range.

---

### `RS_ListRules`

Lists all staged rules with their indices. Works outside setup mode.

```
→ RS_ListRules
← RULES_BEGIN
   0:link:SW1->LED1
   1:toggle:SW1->LED2
   2:adc_pwm:POT->FAN
   3:flash:LED3:500ms/500ms
   RULES_END
```

---

## Saved format (internal)

Pin configurations are stored as JSON under the NVS key `io_config`:

```json
{
  "pins": [
    {"n": "LED1", "t": "dout", "g": 20},
    {"n": "SW1",  "t": "din",  "g": 22, "p": "up"},
    {"n": "VBAT", "t": "adc",  "u": 1,  "c": 6},
    {"n": "FAN",  "t": "pwm",  "g": 5,  "f": 1000}
  ],
  "rules": []
}
```

The `rules` array is reserved for future logic (e.g. input→output linking).
