#include "task.hpp"

Task::Task(const char *name, uint32_t stack_size, UBaseType_t priority)
    : _name(name), _stack_size(stack_size), _priority(priority) {}

void Task::start()
{
    xTaskCreate(trampoline, _name, _stack_size, this, _priority, &_handle);
}

void Task::stop()
{
    if (_handle) {
        vTaskDelete(_handle);
        _handle = nullptr;
    }
}

void Task::trampoline(void *arg)
{
    static_cast<Task *>(arg)->run();
    vTaskDelete(nullptr);
}
