#pragma once

#include <cstddef>
#include <cstdint>
#include "esp_err.h"

class Storage {
public:
    static esp_err_t init();

    explicit Storage(const char *ns);

    esp_err_t putInt(const char *key, int32_t value);
    esp_err_t getInt(const char *key, int32_t &out);

    esp_err_t putBool(const char *key, bool value);
    esp_err_t getBool(const char *key, bool &out);

    esp_err_t putStr(const char *key, const char *value);
    esp_err_t getStr(const char *key, char *buf, size_t buf_len);

private:
    const char *_ns;
};
