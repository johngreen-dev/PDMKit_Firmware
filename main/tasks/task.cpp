#include "task.hpp"

Task::Task(const char *name, uint32_t stack_size, UBaseType_t priority, BaseType_t core_id)
    : _name(name), _stack_size(stack_size), _priority(priority), _core_id(core_id) {}

void Task::start()
{
    xTaskCreatePinnedToCore(trampoline, _name, _stack_size, this, _priority, &_handle, _core_id);
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
