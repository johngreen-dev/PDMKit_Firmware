#include "can_store.hpp"
#include <cstring>

CanStore::CanStore()
{
    _mutex = xSemaphoreCreateMutex();
}

CanStore &CanStore::instance()
{
    static CanStore s_inst;
    return s_inst;
}

void CanStore::updateFrame(uint32_t can_id, const uint8_t *data, uint8_t dlc)
{
    if (!_mutex) return;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    CanFrame &f = _frames[can_id];
    f.dlc  = dlc > 8 ? 8 : dlc;
    f.tick = xTaskGetTickCount();
    memcpy(f.data, data, f.dlc);
    xSemaphoreGive(_mutex);
}

bool CanStore::extractSignal(uint32_t can_id, uint8_t byte_off, uint8_t bit_off,
                              uint8_t bit_len, uint32_t &out) const
{
    if (!_mutex) return false;
    xSemaphoreTake(_mutex, portMAX_DELAY);

    auto it = _frames.find(can_id);
    if (it == _frames.end() || it->second.tick == 0) {
        xSemaphoreGive(_mutex);
        return false;
    }

    const CanFrame &f = it->second;
    // Intel (little-endian) bit ordering: bit_off=0 is LSB of byte_off.
    uint32_t result   = 0;
    uint8_t  start    = (uint8_t)(byte_off * 8u + bit_off);
    for (uint8_t i = 0; i < bit_len; i++) {
        uint8_t abs_bit  = start + i;
        uint8_t byte_idx = abs_bit / 8u;
        uint8_t bit_idx  = abs_bit % 8u;
        if (byte_idx < f.dlc)
            result |= (uint32_t)((f.data[byte_idx] >> bit_idx) & 1u) << i;
    }
    out = result;

    xSemaphoreGive(_mutex);
    return true;
}

TickType_t CanStore::lastTick(uint32_t can_id) const
{
    if (!_mutex) return 0;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    auto it = _frames.find(can_id);
    TickType_t t = (it != _frames.end()) ? it->second.tick : 0;
    xSemaphoreGive(_mutex);
    return t;
}

void CanStore::setMonitoredIds(const std::vector<uint32_t> &ids)
{
    if (!_mutex) return;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _monitored.clear();
    for (auto id : ids)
        _monitored.insert(id);
    xSemaphoreGive(_mutex);
}

bool CanStore::isMonitored(uint32_t can_id) const
{
    if (!_mutex) return false;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    bool found = (_monitored.count(can_id) != 0);
    xSemaphoreGive(_mutex);
    return found;
}
