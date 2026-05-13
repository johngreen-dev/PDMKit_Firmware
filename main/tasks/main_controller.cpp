#include "main_controller.hpp"
#include "io_config.hpp"
#include "gpio.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <vector>
#include <atomic>

#ifndef APP_CPU_NUM
#define APP_CPU_NUM 1
#endif

static const char *TAG = "main_ctrl";

// Runtime state kept alongside each rule (not persisted).
struct RuleState {
    IORule     rule;
    bool       last_src    = false;  // TOGGLE: previous input level
    bool       flash_state = false;  // FLASH: current output level
    TickType_t last_flash  = 0;      // FLASH: tick of last transition
};

MainController::MainController()
    : Task("main_ctrl", 8192, configMAX_PRIORITIES - 1, APP_CPU_NUM)
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

void MainController::requestReload()
{
    _reload.store(true);
}

void MainController::run()
{
    IOConfigManager &io = IOConfigManager::instance();

    while (true) {
        _reload.store(false);
        io.apply();

        if (io.pins().empty()) {
            ESP_LOGI(TAG, "no IO config, running demo");
            runDemo();
            // runDemo() returns when _reload is set — fall through to reload
            continue;
        }

        std::vector<RuleState> states;
        for (const auto &r : io.rules())
            states.push_back({r});

        ESP_LOGI(TAG, "%d pin(s), %d rule(s)", (int)io.pins().size(), (int)states.size());

        TickType_t now = xTaskGetTickCount();
        for (auto &s : states)
            s.last_flash = now;

        while (!_reload.load()) {
            now = xTaskGetTickCount();
            for (auto &s : states)
                evalRule(s, now);
            vTaskDelay(1);
        }
        ESP_LOGI(TAG, "config reload triggered");
    }
}

void MainController::evalRule(RuleState &s, TickType_t now)
{
    const IORule &r = s.rule;
    switch (r.type) {

        case RuleType::LINK: {
            bool val = false;
            if (PinRegistry::read(r.src.c_str(), val) == ESP_OK)
                PinRegistry::set(r.dst.c_str(), val);
            break;
        }

        case RuleType::TOGGLE: {
            bool val = false;
            if (PinRegistry::read(r.src.c_str(), val) == ESP_OK) {
                if (val && !s.last_src)        // rising edge
                    PinRegistry::toggle(r.dst.c_str());
                s.last_src = val;
            }
            break;
        }

        case RuleType::ADC_PWM: {
            int mv = 0;
            if (PinRegistry::readMv(r.src.c_str(), mv) == ESP_OK) {
                float f = (float)mv / 3300.0f; // 0–3.3 V full scale
                if (f < 0.0f) f = 0.0f;
                if (f > 1.0f) f = 1.0f;
                PinRegistry::setDutyF(r.dst.c_str(), f);
            }
            break;
        }

        case RuleType::FLASH: {
            uint32_t interval = s.flash_state ? r.on_ms : r.off_ms;
            if ((now - s.last_flash) >= pdMS_TO_TICKS(interval)) {
                s.flash_state = !s.flash_state;
                PinRegistry::set(r.dst.c_str(), s.flash_state);
                s.last_flash = now;
            }
            break;
        }
    }
}

void MainController::runDemo()
{
    PinRegistry::addOutput("LED1", GPIO_NUM_20);
    PinRegistry::addOutput("LED2", GPIO_NUM_21);
    PinRegistry::addInput ("SW1",  GPIO_NUM_22, GPIO_PULLUP_ENABLE);

    ESP_LOGI(TAG, "SW1 initial raw level: %d", gpio_get_level(GPIO_NUM_22));

    TickType_t lastLed1 = xTaskGetTickCount();
    TickType_t lastLed2 = lastLed1;

    while (!_reload.load()) {
        bool held = false;
        PinRegistry::read("SW1", held);

        if (held) {
            PinRegistry::set("LED1", true);
            PinRegistry::set("LED2", true);
            lastLed1 = xTaskGetTickCount();
            lastLed2 = lastLed1;
            vTaskDelay(1);
            continue;
        }

        TickType_t now = xTaskGetTickCount();
        if ((now - lastLed1) >= pdMS_TO_TICKS(500)) {
            PinRegistry::toggle("LED1");
            lastLed1 = now;
        }
        if ((now - lastLed2) >= pdMS_TO_TICKS(1000)) {
            PinRegistry::toggle("LED2");
            lastLed2 = now;
        }
        vTaskDelay(1);
    }
}
