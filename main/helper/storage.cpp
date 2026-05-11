#include "storage.hpp"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "storage";

esp_err_t Storage::init()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase, reinitialising");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

Storage::Storage(const char *ns) : _ns(ns) {}

Storage &Storage::instance()
{
    static Storage s_instance("storage");
    return s_instance;
}

// --- int ---------------------------------------------------------------------

esp_err_t Storage::_putInt(const char *key, int32_t value)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(_ns, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    err = nvs_set_i32(h, key, value);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t Storage::_getInt(const char *key, int32_t &out)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(_ns, NVS_READONLY, &h);
    if (err != ESP_OK) return err;

    err = nvs_get_i32(h, key, &out);
    nvs_close(h);
    return err;
}

// --- bool --------------------------------------------------------------------

esp_err_t Storage::_putBool(const char *key, bool value)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(_ns, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    err = nvs_set_u8(h, key, value ? 1 : 0);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t Storage::_getBool(const char *key, bool &out)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(_ns, NVS_READONLY, &h);
    if (err != ESP_OK) return err;

    uint8_t raw = 0;
    err = nvs_get_u8(h, key, &raw);
    if (err == ESP_OK) out = (raw != 0);
    nvs_close(h);
    return err;
}

// --- string ------------------------------------------------------------------

esp_err_t Storage::_putStr(const char *key, const char *value)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(_ns, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    err = nvs_set_str(h, key, value);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t Storage::_getStr(const char *key, char *buf, size_t buf_len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(_ns, NVS_READONLY, &h);
    if (err != ESP_OK) return err;

    err = nvs_get_str(h, key, buf, &buf_len);
    nvs_close(h);
    return err;
}
