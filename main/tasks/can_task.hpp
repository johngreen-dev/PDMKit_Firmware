#pragma once

#include "task.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <atomic>
#include <cstdint>

struct CanTxMsg {
    uint32_t can_id;
    uint8_t  data[8];
    uint8_t  dlc;
    bool     ext;   // 29-bit extended ID
};

// Wraps the ESP-IDF TWAI driver for the TJA1051 transceiver.
// GPIO4 = CTX (TX), GPIO5 = CRX (RX), 500 kbps, normal mode.
// RX frames are forwarded to CanStore; TX messages are sent from an internal queue.
class CanTask : public Task {
public:
    static CanTask &instance();
    void start();

    void postTx(uint32_t can_id, const uint8_t *data, uint8_t dlc, bool ext = false);
    bool isBusOff() const { return _bus_off.load(); }

    // Reload baud rate from NVS and restart the TWAI driver.
    void requestReconfig();

private:
    CanTask();
    void run() override;

    QueueHandle_t      _tx_queue{nullptr};
    std::atomic<bool>  _bus_off{false};
    std::atomic<bool>  _reconfig{false};
};
