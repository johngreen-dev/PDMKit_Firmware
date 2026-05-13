#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

class Task {
public:
    explicit Task(const char *name,
                  uint32_t    stack_size = 4096,
                  UBaseType_t priority   = 5,
                  BaseType_t  core_id    = tskNO_AFFINITY);
    virtual ~Task() = default;

    void start();
    void stop();

protected:
    virtual void run() = 0;

private:
    static void trampoline(void *arg);

    const char   *_name;
    uint32_t      _stack_size;
    UBaseType_t   _priority;
    BaseType_t    _core_id;
    TaskHandle_t  _handle = nullptr;
};
