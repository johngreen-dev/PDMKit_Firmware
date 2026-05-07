#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

esp_err_t storage_init(void);

esp_err_t store_int(const char *ns, const char *key, int32_t value);
esp_err_t read_int(const char *ns, const char *key, int32_t *out);

esp_err_t store_bool(const char *ns, const char *key, bool value);
esp_err_t read_bool(const char *ns, const char *key, bool *out);

esp_err_t store_str(const char *ns, const char *key, const char *value);
esp_err_t read_str(const char *ns, const char *key, char *buf, size_t buf_len);
