#include "can_task.hpp"
#include "can_store.hpp"
#include "storage.hpp"
#include "driver/twai.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

static const char *TAG = "can_task";

#define CAN_TX_GPIO      ((gpio_num_t)4)
#define CAN_RX_GPIO      ((gpio_num_t)5)
#define CAN_TX_QUEUE_LEN  8
#define CAN_RX_QUEUE_LEN 16

static twai_timing_config_t baudConfig(int32_t kbps)
{
    switch (kbps) {
        case 125:  return (twai_timing_config_t)TWAI_TIMING_CONFIG_125KBITS();
        case 250:  return (twai_timing_config_t)TWAI_TIMING_CONFIG_250KBITS();
        case 800:  return (twai_timing_config_t)TWAI_TIMING_CONFIG_800KBITS();
        case 1000: return (twai_timing_config_t)TWAI_TIMING_CONFIG_1MBITS();
        default:   return (twai_timing_config_t)TWAI_TIMING_CONFIG_500KBITS();
    }
}

CanTask::CanTask()
    : Task("can_task", 4096, 5, tskNO_AFFINITY)
{}

CanTask &CanTask::instance()
{
    static CanTask s_inst;
    return s_inst;
}

void CanTask::start()
{
    _tx_queue = xQueueCreate(16, sizeof(CanTxMsg));
    Task::start();
}

void CanTask::requestReconfig()
{
    _reconfig.store(true);
}

void CanTask::postTx(uint32_t can_id, const uint8_t *data, uint8_t dlc, bool ext)
{
    if (!_tx_queue) return;
    CanTxMsg msg = {};
    msg.can_id = can_id;
    msg.dlc    = dlc > 8 ? 8 : dlc;
    msg.ext    = ext;
    memcpy(msg.data, data, msg.dlc);
    xQueueSend(_tx_queue, &msg, 0);
}

void CanTask::run()
{
    while (true) {
        _reconfig.store(false);

        // Load baud rate from NVS (default 500 kbps).
        int32_t baud_kbps = 500;
        Storage::instance().getInt("can_baud", baud_kbps);

        twai_general_config_t g_cfg = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_GPIO, CAN_RX_GPIO,
                                                                    TWAI_MODE_NORMAL);
        g_cfg.tx_queue_len = CAN_TX_QUEUE_LEN;
        g_cfg.rx_queue_len = CAN_RX_QUEUE_LEN;

        twai_timing_config_t t_cfg = baudConfig(baud_kbps);
        twai_filter_config_t f_cfg = TWAI_FILTER_CONFIG_ACCEPT_ALL();

        esp_err_t err = twai_driver_install(&g_cfg, &t_cfg, &f_cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "TWAI install failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }
        err = twai_start();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "TWAI start failed: %s", esp_err_to_name(err));
            twai_driver_uninstall();
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }
        ESP_LOGI(TAG, "TWAI started — TX=GPIO%d RX=GPIO%d %ld kbps",
                 (int)CAN_TX_GPIO, (int)CAN_RX_GPIO, baud_kbps);

        CanTxMsg   tx_msg;
        uint32_t   rx_count           = 0;
        uint32_t   tx_count           = 0;
        uint32_t   ignored_count      = 0;
        TickType_t last_status_log    = xTaskGetTickCount();
        bool       was_error_passive  = false;

        while (!_reconfig.load()) {
            TickType_t now = xTaskGetTickCount();
            twai_status_info_t status = {};
            twai_get_status_info(&status);

            // Periodic status dump every 5 s.
            if ((now - last_status_log) >= pdMS_TO_TICKS(5000)) {
                const char *state_str = "UNKNOWN";
                switch (status.state) {
                    case TWAI_STATE_STOPPED:    state_str = "STOPPED";    break;
                    case TWAI_STATE_RUNNING:    state_str = "RUNNING";    break;
                    case TWAI_STATE_BUS_OFF:    state_str = "BUS_OFF";    break;
                    case TWAI_STATE_RECOVERING: state_str = "RECOVERING"; break;
                }
                ESP_LOGI(TAG,
                    "state=%s  baud=%ldkbps  rx=%lu  tx=%lu  ignored=%lu"
                    "  tx_err=%lu  rx_err=%lu  tx_q=%lu  rx_q=%lu"
                    "  arb_lost=%lu  bus_err=%lu",
                    state_str, baud_kbps,
                    (unsigned long)rx_count,
                    (unsigned long)tx_count,
                    (unsigned long)ignored_count,
                    (unsigned long)status.tx_error_counter,
                    (unsigned long)status.rx_error_counter,
                    (unsigned long)status.msgs_to_tx,
                    (unsigned long)status.msgs_to_rx,
                    (unsigned long)status.arb_lost_count,
                    (unsigned long)status.bus_error_count);
                last_status_log = now;
            }

            // Detect error-passive (tx_err >= 128 or rx_err >= 128).
            bool is_error_passive = (status.tx_error_counter >= 128 ||
                                      status.rx_error_counter >= 128);
            if (is_error_passive && !was_error_passive) {
                if (status.rx_error_counter > 0) {
                    // rx_err > 0 means we're seeing bus activity but can't decode it.
                    ESP_LOGW(TAG,
                        "CAN error-passive — rx_err=%lu suggests BAUD RATE MISMATCH "
                        "(current: %ld kbps). Try: RS_SetCANBaud 125 / 250 / 1000. "
                        "Also check 120R termination and CANH/CANL polarity.",
                        (unsigned long)status.rx_error_counter, baud_kbps);
                } else {
                    ESP_LOGW(TAG,
                        "CAN error-passive — tx_err=%lu, no rx_err: "
                        "no other node is ACKing (check 120R termination and second node).",
                        (unsigned long)status.tx_error_counter);
                }
            } else if (!is_error_passive && was_error_passive) {
                ESP_LOGI(TAG, "CAN error-passive cleared — bus healthy at %ld kbps", baud_kbps);
            }
            was_error_passive = is_error_passive;

            // Drain jammed TX queue when the bus is unhealthy.
            if (status.msgs_to_tx >= CAN_TX_QUEUE_LEN && is_error_passive) {
                ESP_LOGW(TAG, "TX queue full while bus unhealthy — clearing %lu stuck frame(s)",
                         (unsigned long)status.msgs_to_tx);
                twai_clear_transmit_queue();
            }

            // Bus-off: auto-recover.
            if (status.state == TWAI_STATE_BUS_OFF) {
                if (!_bus_off.load()) {
                    ESP_LOGW(TAG, "CAN bus-off (tx_err=%lu rx_err=%lu bus_err=%lu) — recovering",
                             (unsigned long)status.tx_error_counter,
                             (unsigned long)status.rx_error_counter,
                             (unsigned long)status.bus_error_count);
                    _bus_off.store(true);
                    twai_initiate_recovery();
                }
            } else if (status.state == TWAI_STATE_RUNNING) {
                if (_bus_off.load()) ESP_LOGI(TAG, "CAN bus recovered");
                _bus_off.store(false);
            }

            // Receive.
            twai_message_t rx = {};
            if (twai_receive(&rx, pdMS_TO_TICKS(10)) == ESP_OK) {
                if (rx.rtr) {
                    ESP_LOGD(TAG, "RX RTR id=0x%lx (skipped)", (unsigned long)rx.identifier);
                } else if (CanStore::instance().isMonitored(rx.identifier)) {
                    CanStore::instance().updateFrame(rx.identifier, rx.data,
                                                     rx.data_length_code);
                    rx_count++;
                    ESP_LOGD(TAG,
                        "RX id=0x%03lx dlc=%u  %02x %02x %02x %02x %02x %02x %02x %02x",
                        (unsigned long)rx.identifier, rx.data_length_code,
                        rx.data[0], rx.data[1], rx.data[2], rx.data[3],
                        rx.data[4], rx.data[5], rx.data[6], rx.data[7]);
                } else {
                    ignored_count++;
                    ESP_LOGD(TAG, "RX ignored id=0x%03lx (not in monitor list)",
                             (unsigned long)rx.identifier);
                }
            }

            // Transmit.
            while (_tx_queue && xQueueReceive(_tx_queue, &tx_msg, 0) == pdTRUE) {
                if (_bus_off.load()) {
                    ESP_LOGW(TAG, "TX dropped id=0x%lx (bus-off)", (unsigned long)tx_msg.can_id);
                    continue;
                }
                twai_message_t m = {};
                m.identifier       = tx_msg.can_id;
                m.extd             = tx_msg.ext ? 1u : 0u;
                m.data_length_code = tx_msg.dlc;
                memcpy(m.data, tx_msg.data, tx_msg.dlc);
                esp_err_t e = twai_transmit(&m, pdMS_TO_TICKS(5));
                if (e == ESP_OK) {
                    tx_count++;
                    ESP_LOGD(TAG,
                        "TX id=0x%03lx dlc=%u  %02x %02x %02x %02x %02x %02x %02x %02x",
                        (unsigned long)tx_msg.can_id, tx_msg.dlc,
                        tx_msg.data[0], tx_msg.data[1], tx_msg.data[2], tx_msg.data[3],
                        tx_msg.data[4], tx_msg.data[5], tx_msg.data[6], tx_msg.data[7]);
                } else {
                    ESP_LOGW(TAG, "TX fail id=0x%lx dlc=%u: %s",
                             (unsigned long)tx_msg.can_id, tx_msg.dlc, esp_err_to_name(e));
                }
            }
        }

        // Reconfig requested — tear down cleanly before next iteration.
        twai_stop();
        twai_driver_uninstall();
        ESP_LOGI(TAG, "TWAI stopped — applying new config");
    }
}
