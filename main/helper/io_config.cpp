#include "io_config.hpp"
#include "io_rules.hpp"
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

void IOConfigManager::clearPins()   { _pins.clear(); }
void IOConfigManager::clearRules()  { _rules.clear(); }
void IOConfigManager::clearVars()   { _vars.clear(); }
void IOConfigManager::clearGroups() { _groups.clear(); }

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
        if (ruleFromJson(item, r)) _rules.push_back(r);
    }

    // --- vars ---
    _vars.clear();
    cJSON *vars_arr = cJSON_GetObjectItem(root, "vars");
    cJSON_ArrayForEach(item, vars_arr) {
        IOVar v;
        cJSON *vn = cJSON_GetObjectItem(item, "name");
        cJSON *ve = cJSON_GetObjectItem(item, "expr");
        if (!cJSON_IsString(vn) || !cJSON_IsString(ve)) continue;
        v.name = vn->valuestring;
        v.expr = ve->valuestring;
        _vars.push_back(v);
    }

    // --- groups ---
    _groups.clear();
    cJSON *groups_arr = cJSON_GetObjectItem(root, "groups");
    cJSON_ArrayForEach(item, groups_arr) {
        IOGroup g;
        cJSON *gn = cJSON_GetObjectItem(item, "name");
        cJSON *gm = cJSON_GetObjectItem(item, "members");
        if (!cJSON_IsString(gn) || !cJSON_IsArray(gm)) continue;
        g.name = gn->valuestring;
        cJSON *mem;
        cJSON_ArrayForEach(mem, gm)
            if (cJSON_IsString(mem)) g.members.push_back(mem->valuestring);
        _groups.push_back(g);
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "loaded %d pin(s), %d rule(s), %d var(s), %d group(s)",
             (int)_pins.size(), (int)_rules.size(), (int)_vars.size(), (int)_groups.size());
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

    for (const auto &r : _rules)
        cJSON_AddItemToArray(rules_arr, ruleToJson(r));

    cJSON *vars_arr2 = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "vars", vars_arr2);
    for (const auto &v : _vars) {
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "name", v.name.c_str());
        cJSON_AddStringToObject(obj, "expr", v.expr.c_str());
        cJSON_AddItemToArray(vars_arr2, obj);
    }

    cJSON *grp_arr = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "groups", grp_arr);
    for (const auto &g : _groups) {
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "name", g.name.c_str());
        cJSON *mem_arr = cJSON_CreateArray();
        for (const auto &m : g.members)
            cJSON_AddItemToArray(mem_arr, cJSON_CreateString(m.c_str()));
        cJSON_AddItemToObject(obj, "members", mem_arr);
        cJSON_AddItemToArray(grp_arr, obj);
    }

    char *str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!str) return ESP_ERR_NO_MEM;

    esp_err_t err = nvsPutStr(NVS_KEY, str);
    cJSON_free(str);
    if (err == ESP_OK)
        ESP_LOGI(TAG, "saved %d pin(s), %d rule(s), %d var(s), %d group(s)",
                 (int)_pins.size(), (int)_rules.size(), (int)_vars.size(), (int)_groups.size());
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

// ---------------------------------------------------------------------------
// Var CRUD
// ---------------------------------------------------------------------------

esp_err_t IOConfigManager::addVar(const IOVar &v)
{
    for (auto &existing : _vars) {
        if (existing.name == v.name) { existing = v; return ESP_OK; }
    }
    _vars.push_back(v);
    return ESP_OK;
}

esp_err_t IOConfigManager::removeVar(const char *name)
{
    auto it = std::find_if(_vars.begin(), _vars.end(),
                           [name](const IOVar &v) { return v.name == name; });
    if (it == _vars.end()) return ESP_ERR_NOT_FOUND;
    _vars.erase(it);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Group CRUD
// ---------------------------------------------------------------------------

esp_err_t IOConfigManager::addGroup(const IOGroup &g)
{
    for (auto &existing : _groups) {
        if (existing.name == g.name) { existing = g; return ESP_OK; }
    }
    _groups.push_back(g);
    return ESP_OK;
}

esp_err_t IOConfigManager::removeGroup(const char *name)
{
    auto it = std::find_if(_groups.begin(), _groups.end(),
                           [name](const IOGroup &g) { return g.name == name; });
    if (it == _groups.end()) return ESP_ERR_NOT_FOUND;
    _groups.erase(it);
    return ESP_OK;
}

const IOGroup *IOConfigManager::findGroup(const char *name) const
{
    for (const auto &g : _groups)
        if (g.name == name) return &g;
    return nullptr;
}
