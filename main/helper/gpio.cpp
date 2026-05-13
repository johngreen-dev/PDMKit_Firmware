#include "gpio.hpp"
#include "esp_log.h"
#include <algorithm>

static const char *TAG = "gpio";

// =============================================================================
// DigitalPin
// =============================================================================

esp_err_t DigitalPin::configOutput(gpio_num_t pin, bool initialLevel)
{
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << pin;
    cfg.mode         = GPIO_MODE_INPUT_OUTPUT;  // INPUT_OUTPUT enables readback of the driven level
    cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type    = GPIO_INTR_DISABLE;

    esp_err_t err = gpio_config(&cfg);
    if (err == ESP_OK)
        gpio_set_level(pin, initialLevel ? 1 : 0);
    return err;
}

esp_err_t DigitalPin::configInput(gpio_num_t pin, gpio_pullup_t pullup, gpio_pulldown_t pulldown)
{
    // Disconnect the pin from any peripheral (USB, JTAG, SPI, …) before
    // configuring it, otherwise the IO_MUX override can silently block
    // the pull-up and drive the line low.
    gpio_reset_pin(pin);

    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << pin;
    cfg.mode         = GPIO_MODE_INPUT;
    cfg.pull_up_en   = pullup;
    cfg.pull_down_en = pulldown;
    cfg.intr_type    = GPIO_INTR_DISABLE;
    return gpio_config(&cfg);
}

void DigitalPin::set(gpio_num_t pin, bool level)
{
    gpio_set_level(pin, level ? 1 : 0);
}

void DigitalPin::toggle(gpio_num_t pin)
{
    // gpio_get_level returns the output latch level for output-mode pins.
    gpio_set_level(pin, gpio_get_level(pin) ^ 1);
}

bool DigitalPin::read(gpio_num_t pin)
{
    return !!gpio_get_level(pin);
}

esp_err_t DigitalPin::attachIsr(gpio_num_t pin, gpio_int_type_t type,
                                  gpio_isr_t handler, void *arg)
{
    gpio_set_intr_type(pin, type);
    esp_err_t err = gpio_install_isr_service(0);
    // ESP_ERR_INVALID_STATE means the service is already installed — that's fine.
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    return gpio_isr_handler_add(pin, handler, arg);
}

// =============================================================================
// AdcPin
// =============================================================================

adc_oneshot_unit_handle_t AdcPin::_handles[2] = {nullptr, nullptr};
adc_cali_handle_t         AdcPin::_cali[2]    = {nullptr, nullptr};

esp_err_t AdcPin::init(adc_unit_t unit)
{
    if (_handles[unit]) return ESP_OK;

    adc_oneshot_unit_init_cfg_t cfg = {};
    cfg.unit_id = unit;
    esp_err_t err = adc_oneshot_new_unit(&cfg, &_handles[unit]);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADC unit %d init failed: %s", unit, esp_err_to_name(err));
        return err;
    }

    // Attempt curve-fitting calibration, fall back to line fitting, then none.
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t ccfg = {};
    ccfg.unit_id  = unit;
    ccfg.atten    = ADC_ATTEN_DB_12;
    ccfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (adc_cali_create_scheme_curve_fitting(&ccfg, &_cali[unit]) != ESP_OK)
        _cali[unit] = nullptr;
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t lcfg = {};
    lcfg.unit_id  = unit;
    lcfg.atten    = ADC_ATTEN_DB_12;
    lcfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (adc_cali_create_scheme_line_fitting(&lcfg, &_cali[unit]) != ESP_OK)
        _cali[unit] = nullptr;
#endif

    if (!_cali[unit])
        ESP_LOGW(TAG, "ADC unit %d: no calibration available", unit);

    return ESP_OK;
}

void AdcPin::deinit(adc_unit_t unit)
{
    if (_cali[unit]) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_delete_scheme_curve_fitting(_cali[unit]);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        adc_cali_delete_scheme_line_fitting(_cali[unit]);
#endif
        _cali[unit] = nullptr;
    }
    if (_handles[unit]) {
        adc_oneshot_del_unit(_handles[unit]);
        _handles[unit] = nullptr;
    }
}

esp_err_t AdcPin::config(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten)
{
    if (!_handles[unit]) return ESP_ERR_INVALID_STATE;

    adc_oneshot_chan_cfg_t cfg = {};
    cfg.atten    = atten;
    cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    return adc_oneshot_config_channel(_handles[unit], channel, &cfg);
}

esp_err_t AdcPin::readRaw(adc_unit_t unit, adc_channel_t channel, int &out)
{
    if (!_handles[unit]) return ESP_ERR_INVALID_STATE;
    return adc_oneshot_read(_handles[unit], channel, &out);
}

esp_err_t AdcPin::readMv(adc_unit_t unit, adc_channel_t channel, int &out_mv)
{
    if (!_cali[unit]) return ESP_ERR_NOT_SUPPORTED;

    int raw = 0;
    esp_err_t err = readRaw(unit, channel, raw);
    if (err != ESP_OK) return err;
    return adc_cali_raw_to_voltage(_cali[unit], raw, &out_mv);
}

// =============================================================================
// PwmPin
// =============================================================================

esp_err_t PwmPin::init(const PwmConfig &cfg)
{
    ledc_timer_config_t timer = {};
    timer.speed_mode      = cfg.mode;
    timer.timer_num       = cfg.timer;
    timer.duty_resolution = cfg.resBits;
    timer.freq_hz         = cfg.freqHz;
    timer.clk_cfg         = LEDC_AUTO_CLK;

    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) return err;

    ledc_channel_config_t ch = {};
    ch.gpio_num   = cfg.pin;
    ch.speed_mode = cfg.mode;
    ch.channel    = cfg.channel;
    ch.timer_sel  = cfg.timer;
    ch.duty       = 0;
    ch.hpoint     = 0;
    return ledc_channel_config(&ch);
}

void PwmPin::deinit(ledc_channel_t channel, ledc_mode_t mode)
{
    ledc_stop(mode, channel, 0);
}

esp_err_t PwmPin::setDuty(ledc_channel_t channel, uint32_t duty, ledc_mode_t mode)
{
    esp_err_t err = ledc_set_duty(mode, channel, duty);
    if (err != ESP_OK) return err;
    return ledc_update_duty(mode, channel);
}

esp_err_t PwmPin::setDutyF(ledc_channel_t channel, float fraction, ledc_mode_t mode,
                            ledc_timer_bit_t resBits)
{
    fraction = std::max(0.0f, std::min(1.0f, fraction));
    uint32_t maxDuty = (1u << resBits) - 1;
    return setDuty(channel, static_cast<uint32_t>(fraction * maxDuty), mode);
}

esp_err_t PwmPin::setFreq(ledc_timer_t timer, uint32_t freqHz, ledc_mode_t mode)
{
    return ledc_set_freq(mode, timer, freqHz);
}

// =============================================================================
// DacPin
// =============================================================================

#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2

esp_err_t DacPin::init(dac_channel_t channel)
{
    return dac_output_enable(channel);
}

void DacPin::deinit(dac_channel_t channel)
{
    dac_output_disable(channel);
}

esp_err_t DacPin::write(dac_channel_t channel, uint8_t value)
{
    return dac_output_voltage(channel, value);
}

#endif

// =============================================================================
// PinRegistry
// =============================================================================

std::map<std::string, PinRegistry::Entry> PinRegistry::_pins;

const PinRegistry::Entry *PinRegistry::_get(const char *name, PinType expected)
{
    auto it = _pins.find(name);
    if (it == _pins.end()) {
        ESP_LOGE(TAG, "PinRegistry: unknown pin '%s'", name);
        return nullptr;
    }
    if (it->second.type != expected) {
        ESP_LOGE(TAG, "PinRegistry: pin '%s' is wrong type", name);
        return nullptr;
    }
    return &it->second;
}

// --- Registration -------------------------------------------------------------

esp_err_t PinRegistry::addOutput(const char *name, gpio_num_t pin, bool initialLevel)
{
    esp_err_t err = DigitalPin::configOutput(pin, initialLevel);
    if (err != ESP_OK) return err;
    Entry e{};
    e.type = PinType::OUTPUT;
    e.gpio = pin;
    _pins[name] = e;
    return ESP_OK;
}

esp_err_t PinRegistry::addInput(const char *name, gpio_num_t pin,
                                  gpio_pullup_t pullup, gpio_pulldown_t pulldown)
{
    esp_err_t err = DigitalPin::configInput(pin, pullup, pulldown);
    if (err != ESP_OK) return err;
    Entry e{};
    e.type      = PinType::INPUT;
    e.gpio      = pin;
    e.activeLow = (pullup == GPIO_PULLUP_ENABLE);
    _pins[name] = e;
    return ESP_OK;
}

esp_err_t PinRegistry::addAdc(const char *name, adc_unit_t unit, adc_channel_t channel,
                                adc_atten_t atten)
{
    esp_err_t err = AdcPin::init(unit);
    if (err != ESP_OK) return err;
    err = AdcPin::config(unit, channel, atten);
    if (err != ESP_OK) return err;
    Entry e{};
    e.type    = PinType::ADC;
    e.adcUnit = unit;
    e.adcCh   = channel;
    _pins[name] = e;
    return ESP_OK;
}

esp_err_t PinRegistry::addPwm(const char *name, const PwmConfig &cfg)
{
    esp_err_t err = PwmPin::init(cfg);
    if (err != ESP_OK) return err;
    Entry e{};
    e.type     = PinType::PWM;
    e.pwmCh    = cfg.channel;
    e.pwmMode  = cfg.mode;
    e.pwmTimer = cfg.timer;
    e.pwmRes   = cfg.resBits;
    _pins[name] = e;
    return ESP_OK;
}

#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2
esp_err_t PinRegistry::addDac(const char *name, dac_channel_t channel)
{
    esp_err_t err = DacPin::init(channel);
    if (err != ESP_OK) return err;
    Entry e{};
    e.type  = PinType::DAC;
    e.dacCh = channel;
    _pins[name] = e;
    return ESP_OK;
}
#endif

// --- Digital ------------------------------------------------------------------

esp_err_t PinRegistry::set(const char *name, bool level)
{
    const Entry *e = _get(name, PinType::OUTPUT);
    if (!e) return ESP_ERR_NOT_FOUND;
    DigitalPin::set(e->gpio, level);
    return ESP_OK;
}

esp_err_t PinRegistry::toggle(const char *name)
{
    const Entry *e = _get(name, PinType::OUTPUT);
    if (!e) return ESP_ERR_NOT_FOUND;
    DigitalPin::toggle(e->gpio);
    return ESP_OK;
}

esp_err_t PinRegistry::read(const char *name, bool &out)
{
    // Allow both input and output pins to be read.
    auto it = _pins.find(name);
    if (it == _pins.end()) {
        ESP_LOGE(TAG, "PinRegistry: unknown pin '%s'", name);
        return ESP_ERR_NOT_FOUND;
    }
    if (it->second.type != PinType::INPUT && it->second.type != PinType::OUTPUT) {
        ESP_LOGE(TAG, "PinRegistry: pin '%s' is not a digital pin", name);
        return ESP_ERR_INVALID_ARG;
    }
    bool raw = DigitalPin::read(it->second.gpio);
    out = it->second.activeLow ? !raw : raw;
    return ESP_OK;
}

// --- ADC ----------------------------------------------------------------------

esp_err_t PinRegistry::readRaw(const char *name, int &out)
{
    const Entry *e = _get(name, PinType::ADC);
    if (!e) return ESP_ERR_NOT_FOUND;
    return AdcPin::readRaw(e->adcUnit, e->adcCh, out);
}

esp_err_t PinRegistry::readMv(const char *name, int &out_mv)
{
    const Entry *e = _get(name, PinType::ADC);
    if (!e) return ESP_ERR_NOT_FOUND;
    return AdcPin::readMv(e->adcUnit, e->adcCh, out_mv);
}

// --- PWM ----------------------------------------------------------------------

esp_err_t PinRegistry::setDuty(const char *name, uint32_t duty)
{
    const Entry *e = _get(name, PinType::PWM);
    if (!e) return ESP_ERR_NOT_FOUND;
    return PwmPin::setDuty(e->pwmCh, duty, e->pwmMode);
}

esp_err_t PinRegistry::setDutyF(const char *name, float fraction)
{
    const Entry *e = _get(name, PinType::PWM);
    if (!e) return ESP_ERR_NOT_FOUND;
    return PwmPin::setDutyF(e->pwmCh, fraction, e->pwmMode, e->pwmRes);
}

esp_err_t PinRegistry::setFreq(const char *name, uint32_t freqHz)
{
    const Entry *e = _get(name, PinType::PWM);
    if (!e) return ESP_ERR_NOT_FOUND;
    return PwmPin::setFreq(e->pwmTimer, freqHz, e->pwmMode);
}

// --- DAC ----------------------------------------------------------------------

esp_err_t PinRegistry::dacWrite(const char *name, uint8_t value)
{
#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2
    const Entry *e = _get(name, PinType::DAC);
    if (!e) return ESP_ERR_NOT_FOUND;
    return DacPin::write(e->dacCh, value);
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
