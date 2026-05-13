#pragma once

#include "task.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Runs on APP_CPU (core 1) at the highest FreeRTOS priority.
// Subclass and override tick() to add control logic.
// tick() is called in a tight loop with no delay — it will fully occupy core 1.
// If you need to yield (e.g. wait on a queue), do it inside tick().

class MainController : public Task {
public:
    static MainController &instance();

    void start();

protected:
    virtual void tick();

private:
    MainController();
    void run() override;

    TickType_t _lastLed1  = 0;
    TickType_t _lastLed2  = 0;
    TickType_t _lastSwLog = 0;
};
