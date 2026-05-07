# Helpers

Reusable ESP-IDF utility modules. Each helper is a single `.h`/`.c` pair.
To add a new helper: create the files here, add the `.c` to `SRCS` in `main/CMakeLists.txt`, then document it below.

---

## storage — NVS key-value store

**Files:** `storage.h` / `storage.c`

Wraps ESP-IDF NVS with simple read/write functions for the three most common types.
NVS keys are scoped by a **namespace** string (max 15 chars) to avoid collisions between modules.

### Setup

Call once at boot before any read/write:

```c
#include "storage.h"

ESP_ERROR_CHECK(storage_init());
```

### API

```c
// Initialise NVS flash (erases and reinitialises if the partition is corrupt)
esp_err_t storage_init(void);

// int32
esp_err_t store_int(const char *ns, const char *key, int32_t value);
esp_err_t read_int (const char *ns, const char *key, int32_t *out);

// bool (stored as uint8)
esp_err_t store_bool(const char *ns, const char *key, bool value);
esp_err_t read_bool (const char *ns, const char *key, bool *out);

// string (null-terminated, max 4000 chars per NVS limit)
esp_err_t store_str(const char *ns, const char *key, const char *value);
esp_err_t read_str (const char *ns, const char *key, char *buf, size_t buf_len);
```

All functions return `ESP_OK` on success.
`read_*` returns `ESP_ERR_NVS_NOT_FOUND` if the key does not exist yet.

### Example

```c
store_int("app",  "boot_count",  42);
store_bool("app", "debug_mode",  true);
store_str("app",  "device_name", "esp32-p4-dev");

int32_t count = 0;
bool    debug = false;
char    name[32] = {0};

read_int("app",  "boot_count",  &count);
read_bool("app", "debug_mode",  &debug);
read_str("app",  "device_name", name, sizeof(name));
```
