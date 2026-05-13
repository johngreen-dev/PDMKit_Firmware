#pragma once

#include <cstdint>
#include <map>
#include <string>
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

// --- Digital input / output --------------------------------------------------

class DigitalPin {
public:
    static esp_err_t configOutput(gpio_num_t pin, bool initialLevel = false);
    static esp_err_t configInput(gpio_num_t pin, gpio_pullup_t pullup = GPIO_PULLUP_DISABLE,
                                  gpio_pulldown_t pulldown = GPIO_PULLDOWN_DISABLE);

    static void      set(gpio_num_t pin, bool level);
    static void      toggle(gpio_num_t pin);
    static bool      read(gpio_num_t pin);

    static esp_err_t attachIsr(gpio_num_t pin, gpio_int_type_t type,
                                gpio_isr_t handler, void *arg);
};

// --- ADC (12-bit, millivolts out) --------------------------------------------

class AdcPin {
public:
    // Call once per ADC unit before using any channel on that unit.
    static esp_err_t init(adc_unit_t unit = ADC_UNIT_1);
    static void      deinit(adc_unit_t unit = ADC_UNIT_1);

    // Configure a single channel. Attenuation controls the full-scale range:
    //   ATTEN_DB_0  ~  0–950 mV
    //   ATTEN_DB_6  ~  0–1.35 V
    //   ATTEN_DB_11 ~  0–3.1 V  (most common)
    static esp_err_t config(adc_unit_t unit, adc_channel_t channel,
                             adc_atten_t atten = ADC_ATTEN_DB_12);

    // Read raw 12-bit value.
    static esp_err_t readRaw(adc_unit_t unit, adc_channel_t channel, int &out);

    // Read calibrated millivolts. Returns ESP_ERR_NOT_SUPPORTED if the chip
    // has no calibration eFuse burned.
    static esp_err_t readMv(adc_unit_t unit, adc_channel_t channel, int &out_mv);

private:
    static adc_oneshot_unit_handle_t _handles[2];   // indexed by adc_unit_t
    static adc_cali_handle_t         _cali[2];
};

// --- PWM (LEDC) --------------------------------------------------------------

struct PwmConfig {
    gpio_num_t         pin;
    ledc_timer_t       timer   = LEDC_TIMER_0;
    ledc_channel_t     channel = LEDC_CHANNEL_0;
    ledc_mode_t        mode    = LEDC_LOW_SPEED_MODE;
    uint32_t           freqHz  = 5000;
    ledc_timer_bit_t   resBits = LEDC_TIMER_13_BIT;  // 0–8191 duty range
};

class PwmPin {
public:
    static esp_err_t init(const PwmConfig &cfg);
    static void      deinit(ledc_channel_t channel, ledc_mode_t mode = LEDC_LOW_SPEED_MODE);

    // duty: 0 → off, (1 << resBits) - 1 → fully on
    static esp_err_t setDuty(ledc_channel_t channel, uint32_t duty,
                              ledc_mode_t mode = LEDC_LOW_SPEED_MODE);

    // Convenience: set 0.0–1.0 as a fraction of full scale.
    // Pass the same resBits you gave to init() so the math is correct.
    static esp_err_t setDutyF(ledc_channel_t channel, float fraction,
                               ledc_mode_t mode = LEDC_LOW_SPEED_MODE,
                               ledc_timer_bit_t resBits = LEDC_TIMER_13_BIT);

    static esp_err_t setFreq(ledc_timer_t timer, uint32_t freqHz,
                              ledc_mode_t mode = LEDC_LOW_SPEED_MODE);
};

// --- Pin registry ------------------------------------------------------------
// Register pins by name once at startup, then reference them by string anywhere.
//
// Example:
//   PinRegistry::addOutput("led",   GPIO_NUM_2);
//   PinRegistry::addInput ("btn",   GPIO_NUM_4, GPIO_PULLUP_ENABLE);
//   PinRegistry::addAdc   ("pot",   ADC_UNIT_1, ADC_CHANNEL_6);
//   PinRegistry::addPwm   ("motor", {.pin = GPIO_NUM_5, .freqHz = 1000});
//
//   PinRegistry::set    ("led", true);
//   PinRegistry::toggle ("led");
//   int mv; PinRegistry::readMv("pot", mv);
//   PinRegistry::setDutyF("motor", 0.75f);

class PinRegistry {
public:
    // --- Registration ---------------------------------------------------------
    static esp_err_t addOutput(const char *name, gpio_num_t pin, bool initialLevel = false);
    static esp_err_t addInput (const char *name, gpio_num_t pin,
                                gpio_pullup_t pullup     = GPIO_PULLUP_DISABLE,
                                gpio_pulldown_t pulldown = GPIO_PULLDOWN_DISABLE);
    static esp_err_t addAdc   (const char *name, adc_unit_t unit, adc_channel_t channel,
                                adc_atten_t atten = ADC_ATTEN_DB_12);
    static esp_err_t addPwm   (const char *name, const PwmConfig &cfg);
#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2
    static esp_err_t addDac   (const char *name, dac_channel_t channel);
#endif

    // --- Digital --------------------------------------------------------------
    static esp_err_t set   (const char *name, bool level);
    static esp_err_t toggle(const char *name);
    static esp_err_t read  (const char *name, bool &out);

    // --- ADC ------------------------------------------------------------------
    static esp_err_t readRaw(const char *name, int &out);
    static esp_err_t readMv (const char *name, int &out_mv);

    // --- PWM ------------------------------------------------------------------
    static esp_err_t setDuty (const char *name, uint32_t duty);
    static esp_err_t setDutyF(const char *name, float fraction);
    static esp_err_t setFreq (const char *name, uint32_t freqHz);

    // --- DAC ------------------------------------------------------------------
    static esp_err_t dacWrite(const char *name, uint8_t value);

private:
    enum class PinType { OUTPUT, INPUT, ADC, PWM, DAC };

    struct Entry {
        PinType type;
        // digital
        gpio_num_t     gpio      = GPIO_NUM_NC;
        bool           activeLow = false;  // true when pull-up used: pressed = GPIO low
        // adc
        adc_unit_t     adcUnit = ADC_UNIT_1;
        adc_channel_t  adcCh   = ADC_CHANNEL_0;
        // pwm
        ledc_channel_t pwmCh   = LEDC_CHANNEL_0;
        ledc_mode_t    pwmMode = LEDC_LOW_SPEED_MODE;
        ledc_timer_t   pwmTimer= LEDC_TIMER_0;
        ledc_timer_bit_t pwmRes= LEDC_TIMER_13_BIT;
#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2
        dac_channel_t  dacCh   = DAC_CHANNEL_1;
#endif
    };

    static std::map<std::string, Entry> _pins;

    static const Entry *_get(const char *name, PinType expected);
};

// --- DAC ---------------------------------------------------------------------
// Available only on chips with a hardware DAC (ESP32 / ESP32-S2).
// Wrap in #ifdef so it compiles cleanly on unsupported targets.

#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2
#include "driver/dac.h"

class DacPin {
public:
    // channel: DAC_CHANNEL_1 (GPIO25) or DAC_CHANNEL_2 (GPIO26) on ESP32
    static esp_err_t init(dac_channel_t channel);
    static void      deinit(dac_channel_t channel);

    // value: 0–255 → 0–VDD
    static esp_err_t write(dac_channel_t channel, uint8_t value);
};
#endif
