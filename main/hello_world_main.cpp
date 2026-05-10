#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "storage.hpp"

static const char *TAG = "hello_world";

extern "C" void app_main(void)
{

    ESP_ERROR_CHECK(Storage::init());

    Storage store("storage");

    ESP_ERROR_CHECK(store.putInt("boot_count",  42));
    ESP_ERROR_CHECK(store.putBool("debug_mode", true));
    ESP_ERROR_CHECK(store.putStr("device_name", "esp32-p4-dev"));

    int32_t boot_count = 0;
    bool    debug_mode = false;
    char    device_name[32] = {};

    ESP_ERROR_CHECK(store.getInt("boot_count",  boot_count));
    ESP_ERROR_CHECK(store.getBool("debug_mode", debug_mode));
    ESP_ERROR_CHECK(store.getStr("device_name", device_name, sizeof(device_name)));

    ESP_LOGI(TAG, "boot_count=%ld  debug_mode=%s  device_name=%s",
             boot_count, debug_mode ? "true" : "false", device_name);

    printf("New Hello C++ World from ESP32-P4!\n");

    int count = 0;
    printf("Count: %d\n", count++);
    while (count < 10) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        printf("Count: %d\n", count++);
    }
    printf("Finished counting to 10.\n");
}
