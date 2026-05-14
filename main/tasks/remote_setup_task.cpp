#include "remote_setup_task.hpp"
#include "io_config.hpp"
#include "gpio.hpp"
#include "expr.hpp"
#include "main_controller.hpp"
#include "io_rules.hpp"
#include "can_task.hpp"
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
#include <cctype>

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
    // Variable config
    else if (strcmp (cmd, "RS_ListVars")          == 0) onListVars();
    else if (strncmp(cmd, "RS_AddVar ",    10)   == 0) onAddVar   (cmd + 10);
    else if (strncmp(cmd, "RS_RemoveVar ", 13)   == 0) onRemoveVar(cmd + 13);
    // Group config
    else if (strcmp (cmd, "RS_ListGroups")        == 0) onListGroups();
    else if (strncmp(cmd, "RS_AddGroup ",  12)   == 0) onAddGroup  (cmd + 12);
    else if (strncmp(cmd, "RS_RemoveGroup ",16)  == 0) onRemoveGroup(cmd + 16);
    // I/O query / control
    else if (strcmp (cmd, "RS_ListPins")          == 0) onListPins();
    else if (strncmp(cmd, "RS_SetOutput ", 13)   == 0) onSetOutput(cmd + 13);
    else if (strncmp(cmd, "RS_GetInput ",  12)   == 0) onGetInput (cmd + 12);
    // CAN runtime config
    else if (strncmp(cmd, "RS_SetCANBaud ", 14)  == 0) onSetCANBaud(cmd + 14);
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

// --- CAN rule parsing helpers ---

static inline bool isCanRuleType(RuleType t)
{
    switch (t) {
        case RuleType::CAN_SIG: case RuleType::CAN_THR: case RuleType::CAN_MAP:
        case RuleType::CAN_TIMEOUT: case RuleType::CAN_MCOND:
        case RuleType::CAN_TX_ST: case RuleType::CAN_TX_AN: case RuleType::CAN_TX_CUR:
        case RuleType::CAN_TX_FLT: case RuleType::CAN_TX_EVT:
        case RuleType::CAN_CMD_OUT: case RuleType::CAN_CMD_FR: case RuleType::CAN_CMD_LC:
        case RuleType::CAN_BOFF: case RuleType::CAN_HTX: case RuleType::CAN_HRX:
        case RuleType::CAN_ELOG: return true;
        default: return false;
    }
}

// Tokenise a string into up to max_tok whitespace-separated tokens.
// Returns the number of tokens found.
static int tokenise(const char *s, char tokens[][32], int max_tok)
{
    int n = 0;
    while (*s && n < max_tok) {
        while (*s && isspace((unsigned char)*s)) s++;
        if (!*s) break;
        int i = 0;
        while (*s && !isspace((unsigned char)*s) && i < 31) tokens[n][i++] = *s++;
        tokens[n][i] = '\0';
        n++;
    }
    return n;
}

// Parse CAN rule arguments from the remainder string (after the type token).
// Wire formats:
//   can_sig     <id> <byte> <bit> <len> <dst> [thresh] [invert]
//   can_thr     <id> <byte> <bit> <len> <dst> <tlo> <thi>
//   can_map     <id> <byte> <bit> <len> <dst> <tlo> <thi> [olo] [ohi]
//   can_timeout <id> <dst> <window_ms> [invert]
//   can_hrx     <id> <dst> <window_ms>
//   can_cmd_out <id> <byte> <cmd_val> <dst>
//   can_tx_st   <src> <id> <interval_ms>
//   can_tx_an   <src> <id> <byte> <interval_ms>
//   can_htx     <id> <interval_ms>
//   can_boff    <dst>
// CAN IDs may be decimal or 0x-prefixed hex.
static bool parseCanRule(RuleType type, const char *args, IORule &r)
{
    char tok[10][32] = {};
    int  n = tokenise(args, tok, 10);

    auto id  = [&](int i) -> uint32_t { return (uint32_t)strtoul(tok[i], nullptr, 0); };
    auto u32 = [&](int i) -> uint32_t { return (uint32_t)strtoul(tok[i], nullptr, 10); };
    auto i32 = [&](int i) -> int32_t  { return (int32_t) strtol (tok[i], nullptr, 10); };

    r.type = type;

    switch (type) {
        case RuleType::CAN_SIG:
            // <id> <byte> <bit> <len> <dst> [thresh] [invert]
            if (n < 5) return false;
            r.can_id    = id(0);
            r.can_byte  = (uint8_t)u32(1);
            r.can_bit   = (uint8_t)u32(2);
            r.can_len   = (uint8_t)u32(3);
            r.dst       = tok[4];
            r.thresh_lo = n >= 6 ? i32(5) : 0;
            r.invert    = n >= 7 && u32(6) != 0;
            break;

        case RuleType::CAN_THR:
            // <id> <byte> <bit> <len> <dst> <tlo> <thi>
            if (n < 7) return false;
            r.can_id    = id(0);
            r.can_byte  = (uint8_t)u32(1);
            r.can_bit   = (uint8_t)u32(2);
            r.can_len   = (uint8_t)u32(3);
            r.dst       = tok[4];
            r.thresh_lo = i32(5);
            r.thresh_hi = i32(6);
            break;

        case RuleType::CAN_MAP:
            // <id> <byte> <bit> <len> <dst> <tlo> <thi> [olo] [ohi]
            if (n < 7) return false;
            r.can_id    = id(0);
            r.can_byte  = (uint8_t)u32(1);
            r.can_bit   = (uint8_t)u32(2);
            r.can_len   = (uint8_t)u32(3);
            r.dst       = tok[4];
            r.thresh_lo = i32(5);
            r.thresh_hi = i32(6);
            r.out_lo    = n >= 8 ? i32(7) : 0;
            r.out_hi    = n >= 9 ? i32(8) : 100;
            break;

        case RuleType::CAN_TIMEOUT:
            // <id> <dst> <window_ms> [invert]
            if (n < 3) return false;
            r.can_id    = id(0);
            r.dst       = tok[1];
            r.window_ms = u32(2);
            r.invert    = n >= 4 && u32(3) != 0;
            break;

        case RuleType::CAN_HRX:
            // <id> <dst> <window_ms>
            if (n < 3) return false;
            r.can_id    = id(0);
            r.dst       = tok[1];
            r.window_ms = u32(2);
            break;

        case RuleType::CAN_CMD_OUT:
            // <id> <byte> <cmd_val> <dst>
            if (n < 4) return false;
            r.can_id    = id(0);
            r.can_byte  = (uint8_t)u32(1);
            r.thresh_lo = i32(2);
            r.dst       = tok[3];
            break;

        case RuleType::CAN_TX_ST:
            // <src> <id> <interval_ms>
            if (n < 3) return false;
            r.srcs.push_back(tok[0]);
            r.can_id  = id(1);
            r.param_b = u32(2);
            break;

        case RuleType::CAN_TX_AN:
            // <src> <id> <byte> <interval_ms>
            if (n < 4) return false;
            r.srcs.push_back(tok[0]);
            r.can_id   = id(1);
            r.can_byte = (uint8_t)u32(2);
            r.param_b  = u32(3);
            break;

        case RuleType::CAN_HTX:
            // <id> <interval_ms>
            if (n < 2) return false;
            r.can_id  = id(0);
            r.param_b = u32(1);
            break;

        case RuleType::CAN_BOFF:
            // <dst>
            if (n < 1) return false;
            r.dst = tok[0];
            break;

        default:
            return false;  // not yet implemented
    }
    return true;
}

// --- Rule config commands ---

void RemoteSetupTask::onAddRule(const char *args)
{
    if (!_setup_mode) { sendResponse("ERR_NOT_IN_SETUP\n"); return; }

    // Extract the type token first
    char type_str[16] = {};
    const char *p = args;
    int i = 0;
    while (*p && !isspace((unsigned char)*p) && i < 15) type_str[i++] = *p++;
    while (*p &&  isspace((unsigned char)*p)) p++;

    IORule r;
    if (!parseRuleType(type_str, r.type)) { sendResponse("ERR_BAD_ARGS\n"); return; }

    // --- CAN rules: bespoke parsing ---
    if (isCanRuleType(r.type)) {
        if (!parseCanRule(r.type, p, r)) { sendResponse("ERR_BAD_ARGS\n"); return; }
        IOConfigManager::instance().addRule(r);
        sendResponse("OK_AddRule\n");
        return;
    }

    // --- EXPR: <dst> <expression text...> ---
    if (r.type == RuleType::EXPR) {
        char dst[32] = {};
        int j = 0;
        while (*p && !isspace((unsigned char)*p) && j < 31) dst[j++] = *p++;
        while (*p &&  isspace((unsigned char)*p)) p++;
        if (!dst[0] || !*p) { sendResponse("ERR_BAD_ARGS\n"); return; }
        std::string err;
        if (!parseExpr(p, err)) {
            char msg[128];
            snprintf(msg, sizeof(msg), "ERR_BAD_EXPR %s\n", err.c_str());
            sendResponse(msg);
            return;
        }
        r.dst  = dst;
        r.expr = p;
        IOConfigManager::instance().addRule(r);
        sendResponse("OK_AddRule\n");
        return;
    }

    // --- All other rule types: use sscanf on the remainder ---
    char arg1[32] = {}, arg2[32] = {};
    uint32_t n1 = 0, n2 = 0;
    int n = sscanf(p, "%31s %31s %lu %lu", arg1, arg2, &n1, &n2);
    if (n < 1) { sendResponse("ERR_BAD_ARGS\n"); return; }

    // Adjust n to account for how many we actually parsed (type already consumed)
    // n here is args parsed from remainder: arg1, arg2, n1, n2

    // n counts tokens in remainder: arg1(1) arg2(2) n1(3) n2(4)
    switch (r.type) {
        // --- Oscillators: RS_AddRule <type> <dst> <n1> <n2> ---
        case RuleType::FLASHER:
        case RuleType::HAZARD:
            if (n < 3) { sendResponse("ERR_BAD_ARGS\n"); return; }
            r.dst    = arg1;
            r.on_ms  = n1;
            r.off_ms = n2;
            break;
        case RuleType::BURST:
            // RS_AddRule burst <dst> <pulse_count> <burst_gap_ms>
            if (n < 3) { sendResponse("ERR_BAD_ARGS\n"); return; }
            r.dst     = arg1;
            r.param_a = n1;
            r.param_b = n2;
            break;
        case RuleType::PWM_OUT:
            // RS_AddRule pwm_out <dst> <freq_hz> <duty_pct>
            if (n < 3) { sendResponse("ERR_BAD_ARGS\n"); return; }
            r.dst     = arg1;
            r.param_a = n1;
            r.param_b = n2;
            break;

        // --- Two-named-input: arg1 encodes "pin1/pin2" ---
        case RuleType::SR_LATCH:
        case RuleType::XOR: {
            // RS_AddRule sr_latch|xor <set_pin>/<reset_pin> <dst>
            if (n < 2) { sendResponse("ERR_BAD_ARGS\n"); return; }
            char *slash = strchr(arg1, '/');
            if (slash) { *slash = '\0'; r.src2 = slash + 1; }
            r.srcs.push_back(arg1);
            r.dst = arg2;
            break;
        }

        // --- All other rules: RS_AddRule <type> <src> <dst> [n1] [n2] ---
        default: {
            if (n < 2) { sendResponse("ERR_BAD_ARGS\n"); return; }
            r.srcs.push_back(arg1);
            r.dst = arg2;

            // NOT always inverts; no extra params needed
            if (r.type == RuleType::NOT) { r.invert = true; break; }

            // Map n1/n2 to the correct IORule fields per type
            switch (r.type) {
                case RuleType::DIRECT:
                case RuleType::NAND_NOR:
                    r.invert = (n1 != 0);
                    break;
                case RuleType::ON_DELAY:
                case RuleType::OFF_DELAY:
                case RuleType::DEBOUNCE:
                    r.delay_ms = n1;
                    break;
                case RuleType::MIN_ON:
                case RuleType::ONE_SHOT:
                case RuleType::PULSE_STR:
                    r.on_ms = n1;
                    break;
                case RuleType::THRESHOLD:
                    r.thresh_lo = (int32_t)n1;
                    r.invert    = (n2 != 0);
                    break;
                case RuleType::HYSTERESIS:
                case RuleType::WINDOW:
                case RuleType::ADC_MAP:
                case RuleType::THERM_DRT:
                    r.thresh_lo = (int32_t)n1;
                    r.thresh_hi = (int32_t)n2;
                    break;
                case RuleType::N_PRESS:
                    r.param_a   = n1;
                    r.window_ms = n2;
                    break;
                case RuleType::OC_LATCH:
                    r.thresh_lo = (int32_t)n1;
                    r.delay_ms  = n2;
                    break;
                case RuleType::RETRY:
                    r.param_a = n1;
                    r.param_b = n2;
                    break;
                case RuleType::WATCHDOG:
                    r.window_ms = n1;
                    break;
                default: break;
            }
            break;
        }
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
        if (r.type == RuleType::EXPR) {
            char hdr[32], tail[64];
            snprintf(hdr,  sizeof(hdr),  "%d:expr:", (int)i);
            snprintf(tail, sizeof(tail), "->%s\n", r.dst.c_str());
            sendResponse(hdr);
            sendResponse(r.expr.c_str());
            sendResponse(tail);
        } else {
            char line[96];
            const char *s0 = r.srcs.empty() ? "" : r.srcs[0].c_str();
            snprintf(line, sizeof(line), "%d:%s:%s->%s\n",
                     (int)i, ruleTypeStr(r.type), s0, r.dst.c_str());
            sendResponse(line);
        }
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

// --- Variable config ---

void RemoteSetupTask::onListVars()
{
    sendResponse("VARS_BEGIN\n");
    for (const auto &v : IOConfigManager::instance().vars()) {
        char line[256];
        snprintf(line, sizeof(line), "%s:%s\n", v.name.c_str(), v.expr.c_str());
        sendResponse(line);
    }
    sendResponse("VARS_END\n");
}

void RemoteSetupTask::onAddVar(const char *args)
{
    if (!_setup_mode) { sendResponse("ERR_NOT_IN_SETUP\n"); return; }
    const char *p = args;
    char name[32] = {};
    int i = 0;
    while (*p && !isspace((unsigned char)*p) && i < 31) name[i++] = *p++;
    while (*p &&  isspace((unsigned char)*p)) p++;
    if (!name[0] || !*p) { sendResponse("ERR_BAD_ARGS\n"); return; }
    std::string err;
    if (!parseExpr(p, err)) {
        char msg[128];
        snprintf(msg, sizeof(msg), "ERR_BAD_EXPR %s\n", err.c_str());
        sendResponse(msg);
        return;
    }
    IOVar v; v.name = name; v.expr = p;
    IOConfigManager::instance().addVar(v);
    sendResponse("OK_AddVar\n");
}

void RemoteSetupTask::onRemoveVar(const char *args)
{
    if (!_setup_mode) { sendResponse("ERR_NOT_IN_SETUP\n"); return; }
    char name[32] = {};
    sscanf(args, "%31s", name);
    esp_err_t e = IOConfigManager::instance().removeVar(name);
    sendResponse(e == ESP_OK ? "OK_RemoveVar\n" : "ERR_NOT_FOUND\n");
}

// --- Group config ---

void RemoteSetupTask::onListGroups()
{
    sendResponse("GROUPS_BEGIN\n");
    for (const auto &g : IOConfigManager::instance().groups()) {
        char line[256];
        std::string members;
        for (size_t i = 0; i < g.members.size(); i++) {
            if (i) members += ",";
            members += g.members[i];
        }
        snprintf(line, sizeof(line), "%s:%s\n", g.name.c_str(), members.c_str());
        sendResponse(line);
    }
    sendResponse("GROUPS_END\n");
}

void RemoteSetupTask::onAddGroup(const char *args)
{
    if (!_setup_mode) { sendResponse("ERR_NOT_IN_SETUP\n"); return; }
    const char *p = args;
    char name[32] = {};
    int i = 0;
    while (*p && !isspace((unsigned char)*p) && i < 31) name[i++] = *p++;
    while (*p &&  isspace((unsigned char)*p)) p++;
    if (!name[0] || !*p) { sendResponse("ERR_BAD_ARGS\n"); return; }
    IOGroup g; g.name = name;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        char mem[32] = {};
        int j = 0;
        while (*p && !isspace((unsigned char)*p) && j < 31) mem[j++] = *p++;
        if (mem[0]) g.members.push_back(mem);
    }
    if (g.members.empty()) { sendResponse("ERR_BAD_ARGS\n"); return; }
    IOConfigManager::instance().addGroup(g);
    sendResponse("OK_AddGroup\n");
}

void RemoteSetupTask::onRemoveGroup(const char *args)
{
    if (!_setup_mode) { sendResponse("ERR_NOT_IN_SETUP\n"); return; }
    char name[32] = {};
    sscanf(args, "%31s", name);
    esp_err_t e = IOConfigManager::instance().removeGroup(name);
    sendResponse(e == ESP_OK ? "OK_RemoveGroup\n" : "ERR_NOT_FOUND\n");
}

// --- CAN runtime config ---

void RemoteSetupTask::onSetCANBaud(const char *args)
{
    int32_t kbps = 0;
    if (sscanf(args, "%ld", &kbps) != 1) {
        sendResponse("ERR_BAD_ARGS\n"); return;
    }
    if (kbps != 125 && kbps != 250 && kbps != 500 && kbps != 800 && kbps != 1000) {
        sendResponse("ERR_BAD_ARGS\n"); return;
    }
    Storage::instance().putInt("can_baud", kbps);
    CanTask::instance().requestReconfig();
    char resp[32];
    snprintf(resp, sizeof(resp), "OK_SetCANBaud %ld\n", kbps);
    sendResponse(resp);
}

void RemoteSetupTask::sendResponse(const char *msg)
{
    tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, (const uint8_t *)msg, strlen(msg));
    tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, pdMS_TO_TICKS(100));
}
