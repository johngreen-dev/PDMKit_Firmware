#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "storage.hpp"
#include "io_config.hpp"
#include "remote_setup_task.hpp"
#include "main_controller.hpp"
#include "can_task.hpp"

static RemoteSetupTask s_remote_setup;

extern "C" void app_main(void)
{
    printf("Welcome to the Power Distribution Module Controller!\n");

    ESP_ERROR_CHECK(Storage::init());
    IOConfigManager::instance().load();

    CanTask::instance().start();
    MainController::instance().start();
    s_remote_setup.start();
}
