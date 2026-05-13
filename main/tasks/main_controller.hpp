#pragma once

#include "task.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <atomic>

struct RuleState;  // defined in main_controller.cpp

class MainController : public Task {
public:
    static MainController &instance();
    void start();
    void requestReload();   // called by RemoteSetupTask after RS_SaveSetup

private:
    MainController();
    void run() override;
    void evalRule(RuleState &s, TickType_t now);
    void runDemo();

    std::atomic<bool> _reload{false};
};
