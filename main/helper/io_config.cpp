#include "io_config.hpp"
#include "gpio.hpp"
#include "cJSON.h"
#include "esp_log.h"
#include "nvs.h"
#include <algorithm>
#include <cstring>

static const char *TAG     = "io_cfg";
static const char *NVS_NS  = "storage";
static const char *NVS_KEY = "io_config";

IOConfigManager &IOConfigManager::instance()
{
    static IOConfigManager s;
    return s;
}

void IOConfigManager::clearPins()  { _pins.clear(); }
void IOConfigManager::clearRules() { _rules.clear(); }

// ---------------------------------------------------------------------------
// Serialisation helpers — pins
// ---------------------------------------------------------------------------

static const char *pinTypeStr(PinType t)
{
    switch (t) {
        case PinType::OUTPUT: return "dout";
        case PinType::INPUT:  return "din";
        case PinType::ADC:    return "adc";
        case PinType::PWM:    return "pwm";
    }
    return "dout";
}

static bool parsePinType(const char *s, PinType &out)
{
    if (!s) return false;
    if (strcmp(s, "dout") == 0) { out = PinType::OUTPUT; return true; }
    if (strcmp(s, "din")  == 0) { out = PinType::INPUT;  return true; }
    if (strcmp(s, "adc")  == 0) { out = PinType::ADC;    return true; }
    if (strcmp(s, "pwm")  == 0) { out = PinType::PWM;    return true; }
    return false;
}

static const char *pullStr(int p)
{
    if (p == 1) return "up";
    if (p == 2) return "down";
    return "none";
}

static int parsePull(const char *s)
{
    if (!s) return 0;
    if (strcmp(s, "up")   == 0) return 1;
    if (strcmp(s, "down") == 0) return 2;
    return 0;
}

// ---------------------------------------------------------------------------
// Serialisation helpers — rules
// ---------------------------------------------------------------------------

static const char *ruleTypeStr(RuleType t)
{
    switch (t) {
        case RuleType::LINK:    return "link";
        case RuleType::TOGGLE:  return "toggle";
        case RuleType::ADC_PWM: return "adc_pwm";
        case RuleType::FLASH:   return "flash";
    }
    return "link";
}

static bool parseRuleType(const char *s, RuleType &out)
{
    if (!s) return false;
    if (strcmp(s, "link")    == 0) { out = RuleType::LINK;    return true; }
    if (strcmp(s, "toggle")  == 0) { out = RuleType::TOGGLE;  return true; }
    if (strcmp(s, "adc_pwm") == 0) { out = RuleType::ADC_PWM; return true; }
    if (strcmp(s, "flash")   == 0) { out = RuleType::FLASH;   return true; }
    return false;
}

// ---------------------------------------------------------------------------
// NVS raw helpers (bypass Storage template for runtime key)
// ---------------------------------------------------------------------------

static esp_err_t nvsGetStr(const char *key, char *buf, size_t len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    err = nvs_get_str(h, key, buf, &len);
    nvs_close(h);
    return err;
}

static esp_err_t nvsPutStr(const char *key, const char *value)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, key, value);
    if (err == ESP_OK) nvs_commit(h);
    nvs_close(h);
    return err;
}

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------

esp_err_t IOConfigManager::load()
{
    char *buf = new char[2048];
    esp_err_t err = nvsGetStr(NVS_KEY, buf, 2048);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "no io_config in NVS (%s)", esp_err_to_name(err));
        delete[] buf;
        return ESP_OK;
    }

    cJSON *root = cJSON_Parse(buf);
    delete[] buf;
    if (!root) {
        ESP_LOGW(TAG, "JSON parse error");
        return ESP_ERR_INVALID_STATE;
    }

    // --- pins ---
    _pins.clear();
    cJSON *pins_arr = cJSON_GetObjectItem(root, "pins");
    cJSON *item;
    cJSON_ArrayForEach(item, pins_arr) {
        IOPin p;
        cJSON *n = cJSON_GetObjectItem(item, "n");
        cJSON *t = cJSON_GetObjectItem(item, "t");
        if (!cJSON_IsString(n) || !cJSON_IsString(t)) continue;
        p.name = n->valuestring;
        if (!parsePinType(t->valuestring, p.type)) continue;

        cJSON *g  = cJSON_GetObjectItem(item, "g");
        if (cJSON_IsNumber(g))  p.gpio    = (gpio_num_t)g->valueint;

        cJSON *pu = cJSON_GetObjectItem(item, "p");
        if (cJSON_IsString(pu)) p.pull    = parsePull(pu->valuestring);

        cJSON *u  = cJSON_GetObjectItem(item, "u");
        if (cJSON_IsNumber(u))  p.adcUnit = u->valueint;

        cJSON *c  = cJSON_GetObjectItem(item, "c");
        if (cJSON_IsNumber(c))  p.adcCh   = c->valueint;

        cJSON *f  = cJSON_GetObjectItem(item, "f");
        if (cJSON_IsNumber(f))  p.freq    = (uint32_t)f->valuedouble;

        _pins.push_back(p);
    }

    // --- rules ---
    _rules.clear();
    cJSON *rules_arr = cJSON_GetObjectItem(root, "rules");
    cJSON_ArrayForEach(item, rules_arr) {
        IORule r;
        cJSON *t = cJSON_GetObjectItem(item, "t");
        if (!cJSON_IsString(t)) continue;
        if (!parseRuleType(t->valuestring, r.type)) continue;

        cJSON *src = cJSON_GetObjectItem(item, "src");
        cJSON *dst = cJSON_GetObjectItem(item, "dst");
        cJSON *on  = cJSON_GetObjectItem(item, "on");
        cJSON *off = cJSON_GetObjectItem(item, "off");

        if (cJSON_IsString(src)) r.src    = src->valuestring;
        if (cJSON_IsString(dst)) r.dst    = dst->valuestring;
        if (cJSON_IsNumber(on))  r.on_ms  = (uint32_t)on->valuedouble;
        if (cJSON_IsNumber(off)) r.off_ms = (uint32_t)off->valuedouble;

        _rules.push_back(r);
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "loaded %d pin(s), %d rule(s)", (int)_pins.size(), (int)_rules.size());
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Save
// ---------------------------------------------------------------------------

esp_err_t IOConfigManager::save()
{
    cJSON *root      = cJSON_CreateObject();
    cJSON *pins_arr  = cJSON_CreateArray();
    cJSON *rules_arr = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "pins",  pins_arr);
    cJSON_AddItemToObject(root, "rules", rules_arr);

    for (const auto &p : _pins) {
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "n", p.name.c_str());
        cJSON_AddStringToObject(obj, "t", pinTypeStr(p.type));
        if (p.type != PinType::ADC)
            cJSON_AddNumberToObject(obj, "g", (double)p.gpio);
        if (p.type == PinType::INPUT)
            cJSON_AddStringToObject(obj, "p", pullStr(p.pull));
        if (p.type == PinType::ADC) {
            cJSON_AddNumberToObject(obj, "u", p.adcUnit);
            cJSON_AddNumberToObject(obj, "c", p.adcCh);
        }
        if (p.type == PinType::PWM)
            cJSON_AddNumberToObject(obj, "f", (double)p.freq);
        cJSON_AddItemToArray(pins_arr, obj);
    }

    for (const auto &r : _rules) {
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "t", ruleTypeStr(r.type));
        if (!r.src.empty()) cJSON_AddStringToObject(obj, "src", r.src.c_str());
        if (!r.dst.empty()) cJSON_AddStringToObject(obj, "dst", r.dst.c_str());
        if (r.type == RuleType::FLASH) {
            cJSON_AddNumberToObject(obj, "on",  (double)r.on_ms);
            cJSON_AddNumberToObject(obj, "off", (double)r.off_ms);
        }
        cJSON_AddItemToArray(rules_arr, obj);
    }

    char *str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!str) return ESP_ERR_NO_MEM;

    esp_err_t err = nvsPutStr(NVS_KEY, str);
    cJSON_free(str);
    if (err == ESP_OK)
        ESP_LOGI(TAG, "saved %d pin(s), %d rule(s)", (int)_pins.size(), (int)_rules.size());
    else
        ESP_LOGW(TAG, "save failed: %s", esp_err_to_name(err));
    return err;
}

// ---------------------------------------------------------------------------
// Apply
// ---------------------------------------------------------------------------

esp_err_t IOConfigManager::apply()
{
    for (const auto &p : _pins) {
        esp_err_t e = ESP_OK;
        switch (p.type) {
            case PinType::OUTPUT:
                e = PinRegistry::addOutput(p.name.c_str(), p.gpio);
                break;
            case PinType::INPUT: {
                auto pu = (p.pull == 1) ? GPIO_PULLUP_ENABLE   : GPIO_PULLUP_DISABLE;
                auto pd = (p.pull == 2) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;
                e = PinRegistry::addInput(p.name.c_str(), p.gpio, pu, pd);
                break;
            }
            case PinType::ADC:
                e = PinRegistry::addAdc(p.name.c_str(),
                                        (adc_unit_t)p.adcUnit,
                                        (adc_channel_t)p.adcCh);
                break;
            case PinType::PWM: {
                PwmConfig cfg{};
                cfg.pin    = p.gpio;
                cfg.freqHz = p.freq;
                e = PinRegistry::addPwm(p.name.c_str(), cfg);
                break;
            }
        }
        if (e != ESP_OK)
            ESP_LOGW(TAG, "apply '%s': %s", p.name.c_str(), esp_err_to_name(e));
        else
            ESP_LOGI(TAG, "applied '%s' (%s gpio%d)", p.name.c_str(), pinTypeStr(p.type), (int)p.gpio);
    }
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Pin CRUD
// ---------------------------------------------------------------------------

esp_err_t IOConfigManager::addPin(const IOPin &pin)
{
    for (auto &p : _pins) {
        if (p.name == pin.name) { p = pin; return ESP_OK; }
    }
    _pins.push_back(pin);
    return ESP_OK;
}

esp_err_t IOConfigManager::removePin(const char *name)
{
    auto it = std::find_if(_pins.begin(), _pins.end(),
                           [name](const IOPin &p) { return p.name == name; });
    if (it == _pins.end()) return ESP_ERR_NOT_FOUND;
    _pins.erase(it);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Rule CRUD
// ---------------------------------------------------------------------------

esp_err_t IOConfigManager::addRule(const IORule &rule)
{
    _rules.push_back(rule);
    return ESP_OK;
}

esp_err_t IOConfigManager::removeRule(size_t index)
{
    if (index >= _rules.size()) return ESP_ERR_NOT_FOUND;
    _rules.erase(_rules.begin() + index);
    return ESP_OK;
}
