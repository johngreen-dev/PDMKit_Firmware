#pragma once

#include <string>
#include <vector>
#include "cJSON.h"

// ---------------------------------------------------------------------------
// Rule types
// ---------------------------------------------------------------------------

enum class RuleType {
    // --- Expression ---
    EXPR,       // boolean expression: expr field -> dst pin/group

    // --- Combinational ---
    DIRECT,     // src -> dst (direct copy, optional invert)
    AND,        // all srcs high -> dst high
    OR,         // any src high -> dst high
    NOT,        // !srcs[0] -> dst (DIRECT with invert=true)
    NAND_NOR,   // !(AND/OR result) depending on invert; srcs determine AND vs OR
    XOR,        // srcs[0] ^ srcs[1] -> dst

    // --- Timing ---
    ON_DELAY,   // src rising -> delay_ms -> dst high
    OFF_DELAY,  // src falling -> delay_ms -> dst low
    MIN_ON,     // src pulses shorter than on_ms are suppressed
    ONE_SHOT,   // src rising edge -> dst high for on_ms then off
    PULSE_STR,  // any src pulse stretched to at least on_ms
    DEBOUNCE,   // src must be stable for delay_ms before dst follows

    // --- Oscillator ---
    FLASHER,    // free-running: dst on for on_ms, off for off_ms
    HAZARD,     // FLASHER for param_a cycles then stops (0 = infinite)
    BURST,      // param_a pulses per burst (on_ms on / off_ms off), param_b ms between bursts
    PWM_OUT,    // dst PWM: param_a Hz frequency, param_b duty % (0-100)

    // --- Threshold ---
    THRESHOLD,  // srcs[0] ADC mv > thresh_lo -> dst (invert: < thresh_lo)
    HYSTERESIS, // srcs[0] > thresh_hi -> latch on; < thresh_lo -> latch off
    WINDOW,     // thresh_lo < srcs[0] < thresh_hi -> dst (invert: out-of-window)
    ADC_MAP,    // srcs[0] in [thresh_lo, thresh_hi] mv -> dst PWM duty in [out_lo, out_hi] %

    // --- Stateful ---
    SR_LATCH,   // srcs[0]=set, src2=reset -> dst (dominant-reset SR)
    TOGGLE,     // srcs[0] rising edge flips dst
    INTERLOCK,  // only the highest-index active src in srcs[] drives dst; others block it
    PRIO_OR,    // first active src (priority order) drives dst
    N_PRESS,    // param_a presses of srcs[0] within window_ms -> dst pulse (on_ms)

    // --- Protective ---
    OC_LATCH,   // srcs[0] ADC mv > thresh_lo for delay_ms -> dst latches off until manual reset
    RETRY,      // on fault (srcs[0] low) de-asserts dst; retries param_a times, param_b ms backoff
    THERM_DRT,  // srcs[0] ADC mv: below thresh_lo -> full, above thresh_hi -> zero, linear derate between
    WATCHDOG,   // srcs[0] must edge within window_ms or dst de-asserts

    // --- CAN -- placeholders, not yet implemented ---
    CAN_SIG,        // extract CAN signal value -> compare threshold -> GPIO
    CAN_THR,        // CAN signal threshold comparison -> GPIO
    CAN_MAP,        // CAN signal linear mapping -> GPIO/PWM
    CAN_TIMEOUT,    // CAN signal absence timeout -> GPIO fallback
    CAN_MCOND,      // multiple CAN signal conditions -> GPIO

    CAN_TX_ST,      // GPIO state -> periodic CAN status frame
    CAN_TX_AN,      // ADC reading -> CAN analog frame
    CAN_TX_CUR,     // current-sense ADC -> CAN current frame
    CAN_TX_FLT,     // fault/latch state -> CAN fault frame
    CAN_TX_EVT,     // GPIO edge -> CAN event frame

    CAN_CMD_OUT,    // incoming CAN command -> GPIO output control
    CAN_CMD_FR,     // incoming CAN command -> fault latch reset
    CAN_CMD_LC,     // incoming CAN command -> live config update

    CAN_BOFF,       // CAN bus-off recovery handler
    CAN_HTX,        // CAN heartbeat transmit
    CAN_HRX,        // CAN heartbeat receive watchdog -> GPIO
    CAN_ELOG,       // CAN error-counter logging
};

// ---------------------------------------------------------------------------
// Rule descriptor
// ---------------------------------------------------------------------------

struct IORule {
    RuleType type = RuleType::DIRECT;

    // Source pins -- most rules use srcs[0]; multi-input rules (AND/OR/etc.) use srcs[0..n-1]
    std::vector<std::string> srcs;
    std::string src2;   // secondary named input: SR_LATCH reset pin
    std::string dst;    // destination / output pin

    // Timing (ms) -- each field's meaning is rule-specific (see RuleType comments above)
    uint32_t on_ms     = 0;
    uint32_t off_ms    = 0;
    uint32_t delay_ms  = 0;
    uint32_t window_ms = 0;

    // Integer rule params (rule-specific -- see RuleType comments above)
    uint32_t param_a   = 0;   // burst count, N_PRESS N, max_retries, PWM_OUT freq Hz, HAZARD cycle count
    uint32_t param_b   = 0;   // BURST inter-burst gap ms, RETRY backoff ms, PWM_OUT duty %

    // Threshold / range in millivolts or raw ADC counts
    int32_t  thresh_lo = 0;
    int32_t  thresh_hi = 0;

    // Output range (ADC_MAP, THERM_DRT) as percent or duty units
    int32_t  out_lo    = 0;
    int32_t  out_hi    = 100;

    bool     invert    = false;   // invert logic output (NOT, NAND_NOR, THRESHOLD below, WINDOW out-of-window)

    std::string expr;  // EXPR: the boolean expression text

    // CAN fields (only set for CAN_* rule types)
    uint32_t can_id   = 0;    // CAN message ID (11-bit standard or 29-bit extended)
    uint8_t  can_byte = 0;    // byte offset in CAN data frame (0-7)
    uint8_t  can_bit  = 0;    // bit offset within byte, LSB-first (0-7)
    uint8_t  can_len  = 8;    // signal bit width (1-32)
    bool     can_ext  = false; // true = 29-bit extended ID
};

// ---------------------------------------------------------------------------
// Variable and Group descriptors
// ---------------------------------------------------------------------------

struct IOVar {
    std::string name;
    std::string expr;
};

struct IOGroup {
    std::string              name;
    std::vector<std::string> members;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

const char *ruleTypeStr(RuleType t);
bool        parseRuleType(const char *s, RuleType &out);

cJSON      *ruleToJson(const IORule &r);
bool        ruleFromJson(const cJSON *obj, IORule &out);
