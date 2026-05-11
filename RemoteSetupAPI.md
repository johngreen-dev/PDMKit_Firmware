# PDMKit Remote Setup API

Commands are sent as plain text over USB CDC (the device enumerates as **PDMKit_Controller**).
Each command is a single line terminated with `\n`. Responses are returned the same way.

---

## Transport

| Property    | Value              |
|-------------|--------------------|
| Interface   | USB CDC (virtual serial port) |
| Product name | `PDMKit_Controller` |
| Baud rate   | Any (USB CDC ignores baud) |
| Line ending | `\n` or `\r\n`    |
| Encoding    | ASCII              |

---

## Commands

### `RS_StartSetup`

Enters remote setup mode. The device will accept further configuration commands until
`RS_SaveSetup` or `RS_CancelSetup` is received.

**Request**
```
RS_StartSetup
```

**Response**
```
OK_StartSetup
```

---

### `RS_SaveSetup`

Commits any configuration changes made during the setup session and exits setup mode.

**Request**
```
RS_SaveSetup
```

**Response**
```
OK_SaveSetup
```

---

### `RS_CancelSetup`

Discards any uncommitted configuration changes and exits setup mode.

**Request**
```
RS_CancelSetup
```

**Response**
```
OK_CancelSetup
```

---

### `RS_GetStorage`

Reads all entries in the `storage` NVS namespace and returns them as a delimited block.

**Request**
```
RS_GetStorage
```

**Response**
```
STORAGE_BEGIN
<key>:<type>:<value>
...
STORAGE_END
```

Types: `int`, `bool`, `str`, `unknown`. Example:
```
STORAGE_BEGIN
remote_setup_mode:bool:false
boot_count:int:5
device_name:str:pdm-001
STORAGE_END
```

---

## Error response

Any line that starts with `RS_` but does not match a known command returns:

```
ERR_UNKNOWN_CMD
```

Lines that do not start with `RS_` are silently ignored.

---

## Example session

```
→ RS_StartSetup
← OK_StartSetup

→ RS_SaveSetup
← OK_SaveSetup
```
