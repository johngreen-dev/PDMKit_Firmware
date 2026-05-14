#pragma once

#include <string>
#include <vector>
#include "esp_err.h"
#include "driver/gpio.h"
#include "io_rules.hpp"

// ---------------------------------------------------------------------------
// Pin configuration
// ---------------------------------------------------------------------------

enum class PinType { OUTPUT, INPUT, ADC, PWM };

struct IOPin {
    std::string name;
    PinType     type    = PinType::OUTPUT;
    gpio_num_t  gpio    = GPIO_NUM_NC;  // OUTPUT, INPUT, PWM
    int         pull    = 0;            // INPUT: 0=none 1=up 2=down
    int         adcUnit = 1;            // ADC
    int         adcCh   = 0;            // ADC
    uint32_t    freq    = 5000;         // PWM Hz
};

// ---------------------------------------------------------------------------
// Manager
// ---------------------------------------------------------------------------

// Persists pin configs and logic rules as JSON in NVS ("io_config" key).
// Load at boot, edit during a setup session, save to commit.
class IOConfigManager {
public:
    static IOConfigManager &instance();

    esp_err_t load();   // read from NVS
    esp_err_t save();   // write to NVS
    esp_err_t apply();  // register all pins with PinRegistry

    // Pins
    esp_err_t addPin(const IOPin &pin);    // add or replace by name
    esp_err_t removePin(const char *name);
    void      clearPins();
    const std::vector<IOPin> &pins() const { return _pins; }

    // Rules
    esp_err_t addRule(const IORule &rule);
    esp_err_t removeRule(size_t index);
    void      clearRules();
    const std::vector<IORule> &rules() const { return _rules; }

    // Variables
    esp_err_t addVar(const IOVar &v);      // add or replace by name
    esp_err_t removeVar(const char *name);
    void      clearVars();
    const std::vector<IOVar>   &vars()   const { return _vars;   }

    // Groups
    esp_err_t addGroup(const IOGroup &g);  // add or replace by name
    esp_err_t removeGroup(const char *name);
    void      clearGroups();
    const std::vector<IOGroup> &groups() const { return _groups; }
    const IOGroup *findGroup(const char *name) const;

private:
    IOConfigManager() = default;
    std::vector<IOPin>   _pins;
    std::vector<IORule>  _rules;
    std::vector<IOVar>   _vars;
    std::vector<IOGroup> _groups;
};
