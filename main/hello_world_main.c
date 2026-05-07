#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "storage.h"

static const char *TAG = "hello_world";

void app_main(void)
{
    // ESP_LOGI(TAG, "Hello World from ESP32-P4!");

    // esp_chip_info_t chip_info;
    // esp_chip_info(&chip_info);
    // ESP_LOGI(TAG, "Chip: %d cores, WiFi%s%s, revision %d",
    //          chip_info.cores,
    //          (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
    //          (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "",
    //          chip_info.revision);

    // uint32_t flash_size;
    // if (esp_flash_get_size(NULL, &flash_size) == ESP_OK) {
    //     ESP_LOGI(TAG, "Flash size: %luMB", flash_size / (1024 * 1024));
    // }

    // int count = 0;
    // while (1) {
    //     ESP_LOGI(TAG, "Count: %d", count++);
    //     vTaskDelay(pdMS_TO_TICKS(1000));
    // }

    ESP_ERROR_CHECK(storage_init());

    ESP_ERROR_CHECK(store_int("storage",  "boot_count",  42));
    ESP_ERROR_CHECK(store_bool("storage", "debug_mode",  true));
    ESP_ERROR_CHECK(store_str("storage",  "device_name", "esp32-p4-dev"));

    int32_t boot_count = 0;
    bool    debug_mode = false;
    char    device_name[32] = {0};

    ESP_ERROR_CHECK(read_int("storage",  "boot_count",  &boot_count));
    ESP_ERROR_CHECK(read_bool("storage", "debug_mode",  &debug_mode));
    ESP_ERROR_CHECK(read_str("storage",  "device_name", device_name, sizeof(device_name)));

    ESP_LOGI(TAG, "boot_count=%ld  debug_mode=%s  device_name=%s",
             boot_count, debug_mode ? "true" : "false", device_name);

    printf("New Hello World from ESP32-P4!\n");

    int count = 0;
    printf("Count: %d\n", count++);
    while (count < 10) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        printf("Count: %d\n", count++);
    }
    printf("Finished counting to 10.\n");

}
