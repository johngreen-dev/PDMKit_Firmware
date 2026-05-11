#include "remote_setup_task.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "tinyusb.h"
#include "tinyusb_cdc_acm.h"
#include "storage.hpp"
#include <cstring>

static const char *TAG = "remote_setup";

static RemoteSetupTask *s_instance = nullptr;

RemoteSetupTask::RemoteSetupTask()
    : Task("remote_setup", 8192, 5)
{
    s_instance = this;
}

void RemoteSetupTask::onRxCallback(int itf, cdcacm_event_t *event)
{
    if (!s_instance || !s_instance->_rx_queue) return;

    uint8_t buf[64];
    size_t  rx_size = 0;
    tinyusb_cdcacm_read(static_cast<tinyusb_cdcacm_itf_t>(itf), buf, sizeof(buf), &rx_size);

    for (size_t i = 0; i < rx_size; i++) {
        xQueueSend(s_instance->_rx_queue, &buf[i], 0);
    }
}

void RemoteSetupTask::run()
{
    ESP_LOGI(TAG, "Remote setup task started");

    Storage::instance().getBool("remote_setup_mode", _remote_setup_mode);
    ESP_LOGI(TAG, "remote_setup_mode=%s", _remote_setup_mode ? "true" : "false");

    _rx_queue = xQueueCreate(256, sizeof(uint8_t));

    static const tusb_desc_device_t s_device_desc = {
        .bLength            = sizeof(tusb_desc_device_t),
        .bDescriptorType    = TUSB_DESC_DEVICE,
        .bcdUSB             = 0x0200,
        .bDeviceClass       = TUSB_CLASS_MISC,
        .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
        .bDeviceProtocol    = MISC_PROTOCOL_IAD,
        .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
        .idVendor           = 0x303A,
        .idProduct          = 0x1002,
        .bcdDevice          = 0x0100,
        .iManufacturer      = 0x01,
        .iProduct           = 0x02,
        .iSerialNumber      = 0x03,
        .bNumConfigurations = 0x01,
    };

    static const char *s_str_desc[] = {
        "\x09\x04",          // [0] Language ID: English US
        "PDMKit",            // [1] Manufacturer
        "PDMKit_Controller", // [2] Product
        "PDM-001",           // [3] Serial number
        "PDMKit CDC",        // [4] CDC interface
    };

    tinyusb_config_t tusb_cfg = {
        .port       = TINYUSB_PORT_HIGH_SPEED_0,
        .phy        = { .skip_setup = false, .self_powered = false, .vbus_monitor_io = 0 },
        .task       = { .size = 4096, .priority = 5, .xCoreID = 0 },
        .descriptor = {
            .device            = &s_device_desc,
            .qualifier         = nullptr,
            .string            = s_str_desc,
            .string_count      = 5,
            .full_speed_config = nullptr,
            .high_speed_config = nullptr,
        },
        .event_cb  = nullptr,
        .event_arg = nullptr,
    };
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    tinyusb_config_cdcacm_t acm_cfg = {
        .cdc_port                     = TINYUSB_CDC_ACM_0,
        .callback_rx                  = onRxCallback,
        .callback_rx_wanted_char      = nullptr,
        .callback_line_state_changed  = nullptr,
        .callback_line_coding_changed = nullptr,
    };
    ESP_ERROR_CHECK(tinyusb_cdcacm_init(&acm_cfg));

    ESP_LOGI(TAG, "USB CDC ready");

    uint8_t byte;
    while (true) {
        if (xQueueReceive(_rx_queue, &byte, pdMS_TO_TICKS(100)) != pdTRUE) continue;

        if (byte == '\n' || byte == '\r') {
            if (_line_len > 0) {
                _line_buf[_line_len] = '\0';
                handleCommand(_line_buf);
                _line_len = 0;
            }
        } else if (_line_len < (int)sizeof(_line_buf) - 1) {
            _line_buf[_line_len++] = (char)byte;
        }
    }
}

void RemoteSetupTask::handleCommand(const char *cmd)
{
    if (strncmp(cmd, "RS_", 3) != 0) return;

    ESP_LOGI(TAG, "rx: %s", cmd);

    if      (strcmp(cmd, "RS_StartSetup")  == 0) onStartSetup();
    else if (strcmp(cmd, "RS_SaveSetup")   == 0) onSaveSetup();
    else if (strcmp(cmd, "RS_CancelSetup") == 0) onCancelSetup();
    else sendResponse("ERR_UNKNOWN_CMD\n");
}

void RemoteSetupTask::onStartSetup()
{
    _remote_setup_mode = true;
    Storage::instance().putBool("remote_setup_mode", _remote_setup_mode);
    sendResponse("OK_StartSetup\n");
}

void RemoteSetupTask::onSaveSetup()
{
    _remote_setup_mode = false;
    Storage::instance().putBool("remote_setup_mode", _remote_setup_mode);
    sendResponse("OK_SaveSetup\n");
}

void RemoteSetupTask::onCancelSetup()
{
    _remote_setup_mode = false;
    Storage::instance().putBool("remote_setup_mode", _remote_setup_mode);
    sendResponse("OK_CancelSetup\n");
}

void RemoteSetupTask::sendResponse(const char *msg)
{
    tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, (const uint8_t *)msg, strlen(msg));
    tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, pdMS_TO_TICKS(100));
}
