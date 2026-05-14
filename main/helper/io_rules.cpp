#include "io_rules.hpp"
#include <cstring>

// ---------------------------------------------------------------------------
// Type string lookup table
// ---------------------------------------------------------------------------

static const struct { RuleType type; const char *str; } k_map[] = {
    { RuleType::EXPR,        "expr"      },
    { RuleType::DIRECT,      "direct"    },
    { RuleType::AND,         "and"       },
    { RuleType::OR,          "or"        },
    { RuleType::NOT,         "not"       },
    { RuleType::NAND_NOR,    "nand_nor"  },
    { RuleType::XOR,         "xor"       },

    { RuleType::ON_DELAY,    "on_delay"  },
    { RuleType::OFF_DELAY,   "off_delay" },
    { RuleType::MIN_ON,      "min_on"    },
    { RuleType::ONE_SHOT,    "one_shot"  },
    { RuleType::PULSE_STR,   "pulse_str" },
    { RuleType::DEBOUNCE,    "debounce"  },

    { RuleType::FLASHER,     "flasher"   },
    { RuleType::HAZARD,      "hazard"    },
    { RuleType::BURST,       "burst"     },
    { RuleType::PWM_OUT,     "pwm_out"   },

    { RuleType::THRESHOLD,   "threshold" },
    { RuleType::HYSTERESIS,  "hysteresis"},
    { RuleType::WINDOW,      "window"    },
    { RuleType::ADC_MAP,     "adc_map"   },

    { RuleType::SR_LATCH,    "sr_latch"  },
    { RuleType::TOGGLE,      "toggle"    },
    { RuleType::INTERLOCK,   "interlock" },
    { RuleType::PRIO_OR,     "prio_or"   },
    { RuleType::N_PRESS,     "n_press"   },

    { RuleType::OC_LATCH,    "oc_latch"  },
    { RuleType::RETRY,       "retry"     },
    { RuleType::THERM_DRT,   "therm_drt" },
    { RuleType::WATCHDOG,    "watchdog"  },

    { RuleType::CAN_SIG,     "can_sig"     },
    { RuleType::CAN_THR,     "can_thr"     },
    { RuleType::CAN_MAP,     "can_map"     },
    { RuleType::CAN_TIMEOUT, "can_timeout" },
    { RuleType::CAN_MCOND,   "can_mcond"   },

    { RuleType::CAN_TX_ST,   "can_tx_st"   },
    { RuleType::CAN_TX_AN,   "can_tx_an"   },
    { RuleType::CAN_TX_CUR,  "can_tx_cur"  },
    { RuleType::CAN_TX_FLT,  "can_tx_flt"  },
    { RuleType::CAN_TX_EVT,  "can_tx_evt"  },

    { RuleType::CAN_CMD_OUT, "can_cmd_out" },
    { RuleType::CAN_CMD_FR,  "can_cmd_fr"  },
    { RuleType::CAN_CMD_LC,  "can_cmd_lc"  },

    { RuleType::CAN_BOFF,    "can_boff"    },
    { RuleType::CAN_HTX,     "can_htx"     },
    { RuleType::CAN_HRX,     "can_hrx"     },
    { RuleType::CAN_ELOG,    "can_elog"    },
};
static constexpr int k_map_size = sizeof(k_map) / sizeof(k_map[0]);

const char *ruleTypeStr(RuleType t)
{
    for (int i = 0; i < k_map_size; i++)
        if (k_map[i].type == t) return k_map[i].str;
    return "direct";
}

bool parseRuleType(const char *s, RuleType &out)
{
    if (!s) return false;
    // Legacy compat for rules stored with old type strings
    if (strcmp(s, "link")    == 0) { out = RuleType::DIRECT;  return true; }
    if (strcmp(s, "flash")   == 0) { out = RuleType::FLASHER; return true; }
    if (strcmp(s, "adc_pwm") == 0) { out = RuleType::ADC_MAP; return true; }
    for (int i = 0; i < k_map_size; i++) {
        if (strcmp(k_map[i].str, s) == 0) { out = k_map[i].type; return true; }
    }
    return false;
}

// ---------------------------------------------------------------------------
// JSON serialisation
// ---------------------------------------------------------------------------

cJSON *ruleToJson(const IORule &r)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "t", ruleTypeStr(r.type));

    if (r.srcs.size() == 1) {
        cJSON_AddStringToObject(obj, "src", r.srcs[0].c_str());
    } else if (r.srcs.size() > 1) {
        cJSON *arr = cJSON_CreateArray();
        for (const auto &s : r.srcs)
            cJSON_AddItemToArray(arr, cJSON_CreateString(s.c_str()));
        cJSON_AddItemToObject(obj, "srcs", arr);
    }

    if (!r.src2.empty()) cJSON_AddStringToObject(obj, "src2", r.src2.c_str());
    if (!r.dst.empty())  cJSON_AddStringToObject(obj, "dst",  r.dst.c_str());

    if (r.on_ms)     cJSON_AddNumberToObject(obj, "on",     (double)r.on_ms);
    if (r.off_ms)    cJSON_AddNumberToObject(obj, "off",    (double)r.off_ms);
    if (r.delay_ms)  cJSON_AddNumberToObject(obj, "delay",  (double)r.delay_ms);
    if (r.window_ms) cJSON_AddNumberToObject(obj, "window", (double)r.window_ms);

    if (r.param_a) cJSON_AddNumberToObject(obj, "pa", (double)r.param_a);
    if (r.param_b) cJSON_AddNumberToObject(obj, "pb", (double)r.param_b);

    if (r.thresh_lo) cJSON_AddNumberToObject(obj, "tlo", (double)r.thresh_lo);
    if (r.thresh_hi) cJSON_AddNumberToObject(obj, "thi", (double)r.thresh_hi);
    if (r.out_lo)    cJSON_AddNumberToObject(obj, "olo", (double)r.out_lo);
    if (r.out_hi != 100) cJSON_AddNumberToObject(obj, "ohi", (double)r.out_hi);

    if (r.invert) cJSON_AddBoolToObject(obj, "inv", true);
    if (!r.expr.empty()) cJSON_AddStringToObject(obj, "expr", r.expr.c_str());

    if (r.can_id)        cJSON_AddNumberToObject(obj, "cid", (double)r.can_id);
    if (r.can_byte)      cJSON_AddNumberToObject(obj, "cby", (double)r.can_byte);
    if (r.can_bit)       cJSON_AddNumberToObject(obj, "cbi", (double)r.can_bit);
    if (r.can_len != 8)  cJSON_AddNumberToObject(obj, "cln", (double)r.can_len);
    if (r.can_ext)       cJSON_AddBoolToObject(obj, "cxt", true);

    return obj;
}

bool ruleFromJson(const cJSON *obj, IORule &out)
{
    if (!obj) return false;

    cJSON *t = cJSON_GetObjectItem(obj, "t");
    if (!cJSON_IsString(t) || !parseRuleType(t->valuestring, out.type)) return false;

    out.srcs.clear();
    cJSON *src  = cJSON_GetObjectItem(obj, "src");
    cJSON *srcs = cJSON_GetObjectItem(obj, "srcs");
    if (cJSON_IsString(src)) {
        out.srcs.push_back(src->valuestring);
    } else if (cJSON_IsArray(srcs)) {
        cJSON *item;
        cJSON_ArrayForEach(item, srcs)
            if (cJSON_IsString(item)) out.srcs.push_back(item->valuestring);
    }

    cJSON *src2 = cJSON_GetObjectItem(obj, "src2");
    if (cJSON_IsString(src2)) out.src2 = src2->valuestring;

    cJSON *dst = cJSON_GetObjectItem(obj, "dst");
    if (cJSON_IsString(dst)) out.dst = dst->valuestring;

    auto getU = [&](const char *key) -> uint32_t {
        cJSON *v = cJSON_GetObjectItem(obj, key);
        return cJSON_IsNumber(v) ? (uint32_t)v->valuedouble : 0;
    };
    auto getI = [&](const char *key) -> int32_t {
        cJSON *v = cJSON_GetObjectItem(obj, key);
        return cJSON_IsNumber(v) ? (int32_t)v->valuedouble : 0;
    };

    out.on_ms     = getU("on");
    out.off_ms    = getU("off");
    out.delay_ms  = getU("delay");
    out.window_ms = getU("window");
    out.param_a   = getU("pa");
    out.param_b   = getU("pb");
    out.thresh_lo = getI("tlo");
    out.thresh_hi = getI("thi");
    out.out_lo    = getI("olo");

    cJSON *ohi = cJSON_GetObjectItem(obj, "ohi");
    out.out_hi = cJSON_IsNumber(ohi) ? (int32_t)ohi->valuedouble : 100;

    cJSON *inv = cJSON_GetObjectItem(obj, "inv");
    out.invert = cJSON_IsTrue(inv);

    cJSON *expr = cJSON_GetObjectItem(obj, "expr");
    if (cJSON_IsString(expr)) out.expr = expr->valuestring;

    out.can_id   = getU("cid");
    out.can_byte = (uint8_t)getU("cby");
    out.can_bit  = (uint8_t)getU("cbi");
    { cJSON *v = cJSON_GetObjectItem(obj, "cln");
      out.can_len = cJSON_IsNumber(v) ? (uint8_t)v->valuedouble : 8; }
    { cJSON *v = cJSON_GetObjectItem(obj, "cxt");
      out.can_ext = cJSON_IsTrue(v); }

    return true;
}
