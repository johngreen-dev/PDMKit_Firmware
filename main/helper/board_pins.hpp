#pragma once

// =============================================================================
//  board_pins.hpp — hardware connector label → GPIO / ADC mapping
//
//  Edit BOARD_PIN_TABLE to match your PCB silkscreen.
//  Users send connector labels (e.g. "IO_PIN_1") via RS_ commands; the
//  firmware resolves them to the real GPIO / ADC numbers here.
//
//  resolveGpio()  also accepts raw decimal/hex numbers for backwards compat.
// =============================================================================

#include "driver/gpio.h"
#include <cstring>
#include <cctype>
#include <cstdlib>

struct BoardPinDef {
    const char *label;     // connector silkscreen label
    gpio_num_t  gpio;      // GPIO_NUM_NC if this position has no digital GPIO
    int8_t      adc_unit;  // -1 if not ADC capable
    int8_t      adc_ch;    // -1 if not ADC capable
};

// =============================================================================
//  Hardware map — ESP32-P4
//
//  IO_PIN_1  … IO_PIN_21  — general-purpose connector pins (GPIO order)
//  ADC_PIN_1 … ADC_PIN_5  — J1 ADC inputs (aliases for the five ADC-capable pins)
//
//  NOTE: IO_PIN_3 (GPIO4) = CAN CTX  — avoid using as GPIO when CAN is active.
//        IO_PIN_4 (GPIO5) = CAN CRX  — same.
// =============================================================================
static constexpr BoardPinDef BOARD_PIN_TABLE[] = {
    // label          GPIO           ADC unit  ADC ch
    { "IO_PIN_1",    GPIO_NUM_2,    -1,       -1 },
    { "IO_PIN_2",    GPIO_NUM_3,    -1,       -1 },
    { "IO_PIN_3",    GPIO_NUM_4,    -1,       -1 },  // CAN CTX
    { "IO_PIN_4",    GPIO_NUM_5,    -1,       -1 },  // CAN CRX
    { "IO_PIN_5",    GPIO_NUM_6,    -1,       -1 },
    { "IO_PIN_6",    GPIO_NUM_7,    -1,       -1 },
    { "IO_PIN_7",    GPIO_NUM_8,    -1,       -1 },
    { "IO_PIN_8",    GPIO_NUM_20,   -1,       -1 },
    { "IO_PIN_9",    GPIO_NUM_21,   -1,       -1 },
    { "IO_PIN_10",   GPIO_NUM_22,   -1,       -1 },
    { "IO_PIN_11",   GPIO_NUM_23,   -1,       -1 },
    { "IO_PIN_12",   GPIO_NUM_26,   -1,       -1 },
    { "IO_PIN_13",   GPIO_NUM_27,   -1,       -1 },
    { "IO_PIN_14",   GPIO_NUM_32,   -1,       -1 },
    { "IO_PIN_15",   GPIO_NUM_33,   -1,       -1 },
    { "IO_PIN_16",   GPIO_NUM_36,   -1,       -1 },
    { "IO_PIN_17",   GPIO_NUM_46,    1,        5 },  // J1 pin 36 — ADC1 CH5
    { "IO_PIN_18",   GPIO_NUM_47,    1,        6 },  // J1 pin 37 — ADC1 CH6
    { "IO_PIN_19",   GPIO_NUM_48,    1,        7 },  // J1 pin 33 — ADC1 CH7
    { "IO_PIN_20",   GPIO_NUM_53,    2,        4 },  // J1 pin 35 — ADC2 CH4
    { "IO_PIN_21",   GPIO_NUM_54,    2,        5 },  // J1 pin 32 — ADC2 CH5

    // ADC aliases — same physical pins as IO_PIN_17..21, named for analog use
    { "ADC_PIN_1",   GPIO_NUM_46,    1,        5 },  // J1 pin 36
    { "ADC_PIN_2",   GPIO_NUM_47,    1,        6 },  // J1 pin 37
    { "ADC_PIN_3",   GPIO_NUM_48,    1,        7 },  // J1 pin 33
    { "ADC_PIN_4",   GPIO_NUM_53,    2,        4 },  // J1 pin 35
    { "ADC_PIN_5",   GPIO_NUM_54,    2,        5 },  // J1 pin 32
};

static constexpr int BOARD_PIN_COUNT =
    (int)(sizeof(BOARD_PIN_TABLE) / sizeof(BOARD_PIN_TABLE[0]));

// Find a board pin definition by its connector label.
// Returns nullptr if the label is not in the table.
inline const BoardPinDef *boardPinFind(const char *label)
{
    for (int i = 0; i < BOARD_PIN_COUNT; i++)
        if (strcmp(BOARD_PIN_TABLE[i].label, label) == 0)
            return &BOARD_PIN_TABLE[i];
    return nullptr;
}

// Resolve a GPIO number from a connector label OR a decimal / 0x-hex string.
// Returns GPIO_NUM_NC when the label is unknown or the pin has no GPIO.
inline gpio_num_t resolveGpio(const char *s)
{
    if (!s || !*s) return GPIO_NUM_NC;
    if (!isdigit((unsigned char)*s)) {
        const BoardPinDef *p = boardPinFind(s);
        return (p && p->gpio != GPIO_NUM_NC) ? p->gpio : GPIO_NUM_NC;
    }
    char *end;
    long v = strtol(s, &end, 0);
    return (*end == '\0') ? (gpio_num_t)v : GPIO_NUM_NC;
}

// Resolve an ADC unit + channel from a connector label.
// Returns true and fills unit/ch if the pin has ADC capability; false otherwise.
inline bool resolveAdc(const char *s, int &unit, int &ch)
{
    if (!s || !*s) return false;
    const BoardPinDef *p = boardPinFind(s);
    if (p && p->adc_unit >= 0) {
        unit = p->adc_unit;
        ch   = p->adc_ch;
        return true;
    }
    return false;
}
