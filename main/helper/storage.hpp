#pragma once

#include <cstddef>
#include <cstdint>
#include "esp_err.h"
#include "nvs.h"

class Storage {
public:
    static esp_err_t  init();
    static Storage   &instance();

    // Template wrappers enforce the 15-char NVS key limit at compile time.
    // Passing a string literal that exceeds the limit is a build error.
    template<std::size_t N>
    esp_err_t putInt(const char (&key)[N], int32_t value) {
        static_assert(N <= NVS_KEY_NAME_MAX_SIZE, "NVS key exceeds 15-character limit");
        return _putInt(key, value);
    }
    template<std::size_t N>
    esp_err_t getInt(const char (&key)[N], int32_t &out) {
        static_assert(N <= NVS_KEY_NAME_MAX_SIZE, "NVS key exceeds 15-character limit");
        return _getInt(key, out);
    }
    template<std::size_t N>
    esp_err_t putBool(const char (&key)[N], bool value) {
        static_assert(N <= NVS_KEY_NAME_MAX_SIZE, "NVS key exceeds 15-character limit");
        return _putBool(key, value);
    }
    template<std::size_t N>
    esp_err_t getBool(const char (&key)[N], bool &out) {
        static_assert(N <= NVS_KEY_NAME_MAX_SIZE, "NVS key exceeds 15-character limit");
        return _getBool(key, out);
    }
    template<std::size_t N>
    esp_err_t putStr(const char (&key)[N], const char *value) {
        static_assert(N <= NVS_KEY_NAME_MAX_SIZE, "NVS key exceeds 15-character limit");
        return _putStr(key, value);
    }
    template<std::size_t N>
    esp_err_t getStr(const char (&key)[N], char *buf, size_t buf_len) {
        static_assert(N <= NVS_KEY_NAME_MAX_SIZE, "NVS key exceeds 15-character limit");
        return _getStr(key, buf, buf_len);
    }

private:
    explicit Storage(const char *ns);

    const char *_ns;

    esp_err_t _putInt(const char *key, int32_t value);
    esp_err_t _getInt(const char *key, int32_t &out);
    esp_err_t _putBool(const char *key, bool value);
    esp_err_t _getBool(const char *key, bool &out);
    esp_err_t _putStr(const char *key, const char *value);
    esp_err_t _getStr(const char *key, char *buf, size_t buf_len);
};
