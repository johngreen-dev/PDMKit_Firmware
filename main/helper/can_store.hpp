#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

struct CanFrame {
    uint8_t    data[8] = {};
    uint8_t    dlc     = 0;
    TickType_t tick    = 0;   // 0 = never received
};

// Thread-safe store for the most-recent CAN frame per message ID.
// Populated by CanTask; read by MainController during evalTick.
class CanStore {
public:
    static CanStore &instance();

    // Called by CanTask on each received frame.
    void updateFrame(uint32_t can_id, const uint8_t *data, uint8_t dlc);

    // Extract a bit-field signal from the latest frame for can_id.
    // Intel (little-endian) bit ordering: bit 0 of byte byte_off is the LSB.
    // Returns false if no frame has been received yet for this ID.
    bool extractSignal(uint32_t can_id, uint8_t byte_off, uint8_t bit_off,
                       uint8_t bit_len, uint32_t &out) const;

    // Tick at which the last frame for can_id was received, or 0 if never.
    TickType_t lastTick(uint32_t can_id) const;

    // Called by MainController after a config reload to build the RX filter set.
    void setMonitoredIds(const std::vector<uint32_t> &ids);

    // Used by CanTask to decide whether to store a received frame.
    bool isMonitored(uint32_t can_id) const;

private:
    CanStore();
    mutable SemaphoreHandle_t              _mutex{nullptr};
    std::unordered_map<uint32_t, CanFrame> _frames;
    std::unordered_set<uint32_t>           _monitored;
};
