#include "main_controller.hpp"
#include "gpio.hpp"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"

// APP_CPU_NUM is defined on ESP32/ESP32-S3 but not on all targets (e.g. ESP32-P4).
// Fall back to core 1 directly so the controller always runs on the second core.
#ifndef APP_CPU_NUM
#define APP_CPU_NUM 1
#endif

static const char *TAG = "main_ctrl";

MainController::MainController()
    : Task("main_ctrl",
           8192,
           configMAX_PRIORITIES - 1,
           APP_CPU_NUM)           // core 1; USB/WiFi stack lives on core 0
{}

MainController &MainController::instance()
{
    static MainController s_instance;
    return s_instance;
}

void MainController::start()
{
    ESP_LOGI(TAG, "starting on core %d, priority %d", APP_CPU_NUM, configMAX_PRIORITIES - 1);
    Task::start();
}

void MainController::run()
{
    esp_err_t e;

    e = PinRegistry::addOutput("LED1", GPIO_NUM_20);
    ESP_LOGI(TAG, "addOutput LED1: %s", esp_err_to_name(e));

    e = PinRegistry::addOutput("LED2", GPIO_NUM_21);
    ESP_LOGI(TAG, "addOutput LED2: %s", esp_err_to_name(e));

    e = PinRegistry::addInput("SW1", GPIO_NUM_22, GPIO_PULLUP_ENABLE);
    ESP_LOGI(TAG, "addInput  SW1:  %s", esp_err_to_name(e));

    // Log the raw GPIO level immediately so we can see the default state.
    ESP_LOGI(TAG, "SW1 initial raw level: %d", gpio_get_level(GPIO_NUM_22));

    _lastLed1 = xTaskGetTickCount();
    _lastLed2 = _lastLed1;

    ESP_LOGI(TAG, "running");
    while (true) {
        tick();
    }
}

void MainController::tick()
{
    bool held = false;
    esp_err_t e = PinRegistry::read("SW1", held);

    if (held) {
        PinRegistry::set("LED1", true);
        PinRegistry::set("LED2", true);
        // Reset timers so blinking restarts cleanly on release.
        _lastLed1 = xTaskGetTickCount();
        _lastLed2 = _lastLed1;
        return;
    }

    TickType_t now = xTaskGetTickCount();
    if ((now - _lastLed1) >= pdMS_TO_TICKS(500)) {
        PinRegistry::toggle("LED1");
        _lastLed1 = now;
    }

    if ((now - _lastLed2) >= pdMS_TO_TICKS(1000)) {
        PinRegistry::toggle("LED2");
        _lastLed2 = now;
    }
}
