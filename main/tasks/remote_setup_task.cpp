#include "remote_setup_task.hpp"
#include "io_config.hpp"
#include "gpio.hpp"
#include "main_controller.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "tinyusb.h"
#include "tinyusb_cdc_acm.h"
#include "storage.hpp"
#include "nvs.h"
#include <cstring>
#include <cstdio>

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

    esp_err_t err = Storage::instance().getBool("setup_mode", _setup_mode);
    if (err != ESP_OK) {
        _setup_mode = false;
        Storage::instance().putBool("setup_mode", _setup_mode);
    }
    ESP_LOGI(TAG, "setup_mode=%s", _setup_mode ? "true" : "false");

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

    // session
    if      (strcmp (cmd, "RS_StartSetup")       == 0) onStartSetup();
    else if (strcmp (cmd, "RS_SaveSetup")         == 0) onSaveSetup();
    else if (strcmp (cmd, "RS_CancelSetup")       == 0) onCancelSetup();
    // diagnostic
    else if (strcmp (cmd, "RS_GetStorage")        == 0) onGetStorage();
    // I/O config (require setup mode — checked inside each handler)
    else if (strncmp(cmd, "RS_AddOutput ", 13)   == 0) onAddOutput(cmd + 13);
    else if (strncmp(cmd, "RS_AddInput ",  12)   == 0) onAddInput (cmd + 12);
    else if (strncmp(cmd, "RS_AddADC ",    10)   == 0) onAddADC   (cmd + 10);
    else if (strncmp(cmd, "RS_AddPWM ",    10)   == 0) onAddPWM   (cmd + 10);
    else if (strncmp(cmd, "RS_RemovePin ", 13)   == 0) onRemovePin(cmd + 13);
    // Rule config (require setup mode)
    else if (strncmp(cmd, "RS_AddRule ",   11)   == 0) onAddRule   (cmd + 11);
    else if (strncmp(cmd, "RS_RemoveRule ",14)   == 0) onRemoveRule(cmd + 14);
    else if (strcmp (cmd, "RS_ListRules")         == 0) onListRules();
    // I/O query / control
    else if (strcmp (cmd, "RS_ListPins")          == 0) onListPins();
    else if (strncmp(cmd, "RS_SetOutput ", 13)   == 0) onSetOutput(cmd + 13);
    else if (strncmp(cmd, "RS_GetInput ",  12)   == 0) onGetInput (cmd + 12);
    else sendResponse("ERR_UNKNOWN_CMD\n");
}

// --- session ---

void RemoteSetupTask::onStartSetup()
{
    _setup_mode = true;
    Storage::instance().putBool("setup_mode", true);
    sendResponse("OK_StartSetup\n");
}

void RemoteSetupTask::onSaveSetup()
{
    _setup_mode = false;
    Storage::instance().putBool("setup_mode", false);
    IOConfigManager::instance().save();
    MainController::instance().requestReload();
    sendResponse("OK_SaveSetup\n");
}

void RemoteSetupTask::onCancelSetup()
{
    _setup_mode = false;
    Storage::instance().putBool("setup_mode", false);
    IOConfigManager::instance().load(); // discard staged changes
    sendResponse("OK_CancelSetup\n");
}

// --- diagnostic ---

void RemoteSetupTask::onGetStorage()
{
    sendResponse("STORAGE_BEGIN\n");

    nvs_iterator_t it = nullptr;
    esp_err_t find_err = nvs_entry_find("nvs", "storage", NVS_TYPE_ANY, &it);
    ESP_LOGI(TAG, "nvs_entry_find: %s it=%p", esp_err_to_name(find_err), (void*)it);

    nvs_handle_t h;
    esp_err_t open_err = nvs_open("storage", NVS_READONLY, &h);
    ESP_LOGI(TAG, "nvs_open(READONLY): %s", esp_err_to_name(open_err));

    int count = 0;
    while (it != nullptr) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        count++;

        char line[160];
        switch (info.type) {
            case NVS_TYPE_I32: {
                int32_t val = 0;
                nvs_get_i32(h, info.key, &val);
                snprintf(line, sizeof(line), "%s:int:%ld\n", info.key, val);
                break;
            }
            case NVS_TYPE_U8: {
                uint8_t val = 0;
                nvs_get_u8(h, info.key, &val);
                snprintf(line, sizeof(line), "%s:bool:%s\n", info.key, val ? "true" : "false");
                break;
            }
            case NVS_TYPE_STR: {
                size_t len = 0;
                nvs_get_str(h, info.key, nullptr, &len);
                char *val = new char[len]();
                nvs_get_str(h, info.key, val, &len);
                char hdr[48];
                snprintf(hdr, sizeof(hdr), "%s:str:", info.key);
                sendResponse(hdr);
                sendResponse(val);
                sendResponse("\n");
                delete[] val;
                line[0] = '\0';
                break;
            }
            default:
                snprintf(line, sizeof(line), "%s:unknown:-\n", info.key);
                break;
        }
        sendResponse(line);
        nvs_entry_next(&it);
    }

    ESP_LOGI(TAG, "nvs iterator done, %d entries", count);
    nvs_release_iterator(it);
    nvs_close(h);
    sendResponse("STORAGE_END\n");
}

// --- I/O config commands ---

void RemoteSetupTask::onAddOutput(const char *args)
{
    if (!_setup_mode) { sendResponse("ERR_NOT_IN_SETUP\n"); return; }
    char name[32]; int gpio;
    if (sscanf(args, "%31s %d", name, &gpio) != 2) {
        sendResponse("ERR_BAD_ARGS\n"); return;
    }
    IOPin p; p.name = name; p.type = PinType::OUTPUT; p.gpio = (gpio_num_t)gpio;
    IOConfigManager::instance().addPin(p);
    sendResponse("OK_AddOutput\n");
}

void RemoteSetupTask::onAddInput(const char *args)
{
    if (!_setup_mode) { sendResponse("ERR_NOT_IN_SETUP\n"); return; }
    char name[32]; int gpio; char pull[8] = "none";
    if (sscanf(args, "%31s %d %7s", name, &gpio, pull) < 2) {
        sendResponse("ERR_BAD_ARGS\n"); return;
    }
    IOPin p; p.name = name; p.type = PinType::INPUT; p.gpio = (gpio_num_t)gpio;
    if      (strcmp(pull, "up")   == 0) p.pull = 1;
    else if (strcmp(pull, "down") == 0) p.pull = 2;
    IOConfigManager::instance().addPin(p);
    sendResponse("OK_AddInput\n");
}

void RemoteSetupTask::onAddADC(const char *args)
{
    if (!_setup_mode) { sendResponse("ERR_NOT_IN_SETUP\n"); return; }
    char name[32]; int unit, ch;
    if (sscanf(args, "%31s %d %d", name, &unit, &ch) != 3) {
        sendResponse("ERR_BAD_ARGS\n"); return;
    }
    IOPin p; p.name = name; p.type = PinType::ADC; p.adcUnit = unit; p.adcCh = ch;
    IOConfigManager::instance().addPin(p);
    sendResponse("OK_AddADC\n");
}

void RemoteSetupTask::onAddPWM(const char *args)
{
    if (!_setup_mode) { sendResponse("ERR_NOT_IN_SETUP\n"); return; }
    char name[32]; int gpio; int freq = 5000;
    if (sscanf(args, "%31s %d %d", name, &gpio, &freq) < 2) {
        sendResponse("ERR_BAD_ARGS\n"); return;
    }
    IOPin p; p.name = name; p.type = PinType::PWM;
    p.gpio = (gpio_num_t)gpio; p.freq = (uint32_t)freq;
    IOConfigManager::instance().addPin(p);
    sendResponse("OK_AddPWM\n");
}

void RemoteSetupTask::onRemovePin(const char *args)
{
    if (!_setup_mode) { sendResponse("ERR_NOT_IN_SETUP\n"); return; }
    char name[32];
    if (sscanf(args, "%31s", name) != 1) { sendResponse("ERR_BAD_ARGS\n"); return; }
    esp_err_t e = IOConfigManager::instance().removePin(name);
    sendResponse(e == ESP_OK ? "OK_RemovePin\n" : "ERR_NOT_FOUND\n");
}

// --- Rule config commands ---

void RemoteSetupTask::onAddRule(const char *args)
{
    if (!_setup_mode) { sendResponse("ERR_NOT_IN_SETUP\n"); return; }

    char type_str[16], arg1[32], arg2[32] = {};
    uint32_t n1 = 500, n2 = 500;
    int n = sscanf(args, "%15s %31s %31s %lu %lu", type_str, arg1, arg2, &n1, &n2);
    if (n < 2) { sendResponse("ERR_BAD_ARGS\n"); return; }

    IORule r;
    if      (strcmp(type_str, "link")    == 0) r.type = RuleType::LINK;
    else if (strcmp(type_str, "toggle")  == 0) r.type = RuleType::TOGGLE;
    else if (strcmp(type_str, "adc_pwm") == 0) r.type = RuleType::ADC_PWM;
    else if (strcmp(type_str, "flash")   == 0) r.type = RuleType::FLASH;
    else { sendResponse("ERR_BAD_ARGS\n"); return; }

    if (r.type == RuleType::FLASH) {
        // RS_AddRule flash <dst> <on_ms> <off_ms>
        if (n < 4) { sendResponse("ERR_BAD_ARGS\n"); return; }
        r.dst    = arg1;
        r.on_ms  = n1;
        r.off_ms = n2;
    } else {
        // RS_AddRule link|toggle|adc_pwm <src> <dst>
        if (n < 3) { sendResponse("ERR_BAD_ARGS\n"); return; }
        r.src = arg1;
        r.dst = arg2;
    }

    IOConfigManager::instance().addRule(r);
    sendResponse("OK_AddRule\n");
}

void RemoteSetupTask::onRemoveRule(const char *args)
{
    if (!_setup_mode) { sendResponse("ERR_NOT_IN_SETUP\n"); return; }
    int idx = -1;
    if (sscanf(args, "%d", &idx) != 1 || idx < 0) {
        sendResponse("ERR_BAD_ARGS\n"); return;
    }
    esp_err_t e = IOConfigManager::instance().removeRule((size_t)idx);
    sendResponse(e == ESP_OK ? "OK_RemoveRule\n" : "ERR_NOT_FOUND\n");
}

void RemoteSetupTask::onListRules()
{
    sendResponse("RULES_BEGIN\n");
    const auto &rules = IOConfigManager::instance().rules();
    for (size_t i = 0; i < rules.size(); i++) {
        const IORule &r = rules[i];
        char line[96];
        if (r.type == RuleType::FLASH) {
            snprintf(line, sizeof(line), "%d:flash:%s:%lums/%lums\n",
                     (int)i, r.dst.c_str(),
                     (unsigned long)r.on_ms, (unsigned long)r.off_ms);
        } else {
            const char *ts = (r.type == RuleType::TOGGLE)  ? "toggle"  :
                             (r.type == RuleType::ADC_PWM) ? "adc_pwm" : "link";
            snprintf(line, sizeof(line), "%d:%s:%s->%s\n",
                     (int)i, ts, r.src.c_str(), r.dst.c_str());
        }
        sendResponse(line);
    }
    sendResponse("RULES_END\n");
}

// --- I/O query / control ---

void RemoteSetupTask::onListPins()
{
    sendResponse("PINS_BEGIN\n");
    for (const auto &p : IOConfigManager::instance().pins()) {
        char line[80];
        switch (p.type) {
            case PinType::OUTPUT:
                snprintf(line, sizeof(line), "%s:dout:gpio%d\n", p.name.c_str(), (int)p.gpio);
                break;
            case PinType::INPUT:
                snprintf(line, sizeof(line), "%s:din:gpio%d:%s\n", p.name.c_str(), (int)p.gpio,
                         p.pull == 1 ? "up" : p.pull == 2 ? "down" : "none");
                break;
            case PinType::ADC:
                snprintf(line, sizeof(line), "%s:adc:u%dc%d\n", p.name.c_str(), p.adcUnit, p.adcCh);
                break;
            case PinType::PWM:
                snprintf(line, sizeof(line), "%s:pwm:gpio%d:%luHz\n", p.name.c_str(),
                         (int)p.gpio, (unsigned long)p.freq);
                break;
        }
        sendResponse(line);
    }
    sendResponse("PINS_END\n");
}

void RemoteSetupTask::onSetOutput(const char *args)
{
    char name[32]; int val;
    if (sscanf(args, "%31s %d", name, &val) != 2) {
        sendResponse("ERR_BAD_ARGS\n"); return;
    }
    esp_err_t e = PinRegistry::set(name, val != 0);
    sendResponse(e == ESP_OK ? "OK_SetOutput\n" : "ERR_PIN_NOT_FOUND\n");
}

void RemoteSetupTask::onGetInput(const char *args)
{
    char name[32];
    if (sscanf(args, "%31s", name) != 1) {
        sendResponse("ERR_BAD_ARGS\n"); return;
    }
    bool val = false;
    esp_err_t e = PinRegistry::read(name, val);
    if (e == ESP_OK) {
        char line[48];
        snprintf(line, sizeof(line), "INPUT %s:%d\n", name, val ? 1 : 0);
        sendResponse(line);
    } else {
        sendResponse("ERR_PIN_NOT_FOUND\n");
    }
}

void RemoteSetupTask::sendResponse(const char *msg)
{
    tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, (const uint8_t *)msg, strlen(msg));
    tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, pdMS_TO_TICKS(100));
}
