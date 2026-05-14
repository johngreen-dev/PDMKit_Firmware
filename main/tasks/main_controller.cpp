#include "main_controller.hpp"
#include "io_config.hpp"
#include "gpio.hpp"
#include "expr.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <vector>
#include <map>
#include <string>
#include <atomic>
#include <memory>

#ifndef APP_CPU_NUM
#define APP_CPU_NUM 1
#endif

static const char *TAG = "main_ctrl";

// ---------------------------------------------------------------------------
// Runtime state for each rule
// ---------------------------------------------------------------------------

struct RuleState {
    IORule                    rule;
    bool                      last_src    = false;  // edge detection
    bool                      toggle_state= false;  // TOGGLE / SR_LATCH latch output
    bool                      flash_state = false;  // FLASHER / HAZARD current output
    TickType_t                last_flash  = 0;      // tick of last FLASHER transition
    uint32_t                  hazard_count= 0;      // HAZARD cycles completed
    TickType_t                last_edge   = 0;      // WATCHDOG / N_PRESS last edge tick
    uint32_t                  press_count = 0;      // N_PRESS count within window
    TickType_t                pulse_end   = 0;      // N_PRESS output pulse end tick
    std::unique_ptr<ExprNode> ast;                  // EXPR: parsed AST
};

// ---------------------------------------------------------------------------
// File-local VarState
// ---------------------------------------------------------------------------

struct VarState {
    std::string               name;
    std::unique_ptr<ExprNode> ast;
};

// ---------------------------------------------------------------------------
// Helper: look up a bool signal from the sig map
// ---------------------------------------------------------------------------

static bool sigGet(const std::map<std::string, bool> &sig, const std::string &name)
{
    auto it = sig.find(name);
    return it != sig.end() ? it->second : false;
}

// ---------------------------------------------------------------------------
// computeRule: returns the value this rule claims for its dst.
//   Returns false for rules that handle their own side-effects (PWM/ADC),
//   meaning their return value should NOT be OR'd into the claims map.
//   For those rules the function returns false and the caller skips applying.
// ---------------------------------------------------------------------------

static bool computeRule(RuleState &s, const std::map<std::string, bool> &sig, TickType_t now)
{
    const IORule &r = s.rule;
    const char *s0_name = r.srcs.empty() ? "" : r.srcs[0].c_str();

    switch (r.type) {

        // ---- Expression ----
        case RuleType::EXPR:
            return s.ast ? s.ast->eval(sig) : false;

        // ---- Combinational ----
        case RuleType::DIRECT: {
            bool val = sigGet(sig, r.srcs.empty() ? "" : r.srcs[0]);
            return r.invert ? !val : val;
        }
        case RuleType::NOT: {
            bool val = sigGet(sig, r.srcs.empty() ? "" : r.srcs[0]);
            return !val;
        }
        case RuleType::AND: {
            bool all = true;
            for (const auto &src : r.srcs)
                all = all && sigGet(sig, src);
            return all;
        }
        case RuleType::OR: {
            bool any = false;
            for (const auto &src : r.srcs)
                any = any || sigGet(sig, src);
            return any;
        }
        case RuleType::NAND_NOR: {
            if (!r.invert) {
                // NAND: !(all true)
                bool all = true;
                for (const auto &src : r.srcs) all = all && sigGet(sig, src);
                return !all;
            } else {
                // NOR: !(any true)
                bool any = false;
                for (const auto &src : r.srcs) any = any || sigGet(sig, src);
                return !any;
            }
        }
        case RuleType::XOR: {
            bool a = sigGet(sig, r.srcs.empty() ? "" : r.srcs[0]);
            bool b = sigGet(sig, r.src2);
            return a ^ b;
        }

        // ---- Stateful ----
        case RuleType::TOGGLE: {
            bool val = sigGet(sig, r.srcs.empty() ? "" : r.srcs[0]);
            if (val && !s.last_src)   // rising edge
                s.toggle_state = !s.toggle_state;
            s.last_src = val;
            return s.toggle_state;
        }
        case RuleType::SR_LATCH: {
            bool set_val   = sigGet(sig, r.srcs.empty() ? "" : r.srcs[0]);
            bool reset_val = sigGet(sig, r.src2);
            // Reset dominates
            if (reset_val)
                s.toggle_state = false;
            else if (set_val && !s.last_src)
                s.toggle_state = true;
            s.last_src = set_val;
            return s.toggle_state;
        }

        // ---- Oscillators ----
        case RuleType::FLASHER: {
            uint32_t interval = s.flash_state ? r.on_ms : r.off_ms;
            if (interval > 0 && (now - s.last_flash) >= pdMS_TO_TICKS(interval)) {
                s.flash_state = !s.flash_state;
                s.last_flash  = now;
            }
            return s.flash_state;
        }
        case RuleType::HAZARD: {
            // param_a = cycle count (0 = infinite)
            if (r.param_a > 0 && s.hazard_count >= r.param_a)
                return false;
            uint32_t interval = s.flash_state ? r.on_ms : r.off_ms;
            if (interval > 0 && (now - s.last_flash) >= pdMS_TO_TICKS(interval)) {
                s.flash_state = !s.flash_state;
                s.last_flash  = now;
                if (!s.flash_state)
                    s.hazard_count++;
            }
            return s.flash_state;
        }

        // ---- Threshold ----
        case RuleType::THRESHOLD: {
            int mv = 0;
            if (PinRegistry::readMv(s0_name, mv) != ESP_OK) return false;
            bool above = (mv > r.thresh_lo);
            return r.invert ? !above : above;
        }
        case RuleType::HYSTERESIS: {
            int mv = 0;
            if (PinRegistry::readMv(s0_name, mv) != ESP_OK) return false;
            if (mv > r.thresh_hi)      s.toggle_state = true;
            else if (mv < r.thresh_lo) s.toggle_state = false;
            return s.toggle_state;
        }

        // ---- Watchdog ----
        case RuleType::WATCHDOG: {
            bool val = sigGet(sig, r.srcs.empty() ? "" : r.srcs[0]);
            if (val != s.last_src) {
                s.last_src  = val;
                s.last_edge = now;
            }
            if (r.window_ms == 0) return true;
            return (now - s.last_edge) < pdMS_TO_TICKS(r.window_ms);
        }

        // ---- N_PRESS ----
        case RuleType::N_PRESS: {
            bool val = sigGet(sig, r.srcs.empty() ? "" : r.srcs[0]);
            if (val && !s.last_src) {
                // Rising edge: check window, increment count
                if (s.press_count == 0)
                    s.last_edge = now;
                else if ((now - s.last_edge) > pdMS_TO_TICKS(r.window_ms)) {
                    // window expired, restart
                    s.press_count = 0;
                    s.last_edge   = now;
                }
                s.press_count++;
                if (s.press_count >= r.param_a) {
                    s.press_count = 0;
                    s.pulse_end   = now + pdMS_TO_TICKS(r.on_ms);
                }
            }
            s.last_src = val;
            // Reset count if window expired without threshold
            if (s.press_count > 0 && (now - s.last_edge) > pdMS_TO_TICKS(r.window_ms))
                s.press_count = 0;
            return (now < s.pulse_end);
        }

        // ---- ADC/PWM side-effect rules (no digital output) ----
        case RuleType::ADC_MAP: {
            int mv = 0;
            if (PinRegistry::readMv(s0_name, mv) == ESP_OK) {
                float range = (float)(r.thresh_hi - r.thresh_lo);
                float f = (range > 0.0f) ?
                    ((float)(mv - r.thresh_lo) / range) : 0.0f;
                if (f < 0.0f) f = 0.0f;
                if (f > 1.0f) f = 1.0f;
                float duty = r.out_lo + f * (r.out_hi - r.out_lo);
                PinRegistry::setDutyF(r.dst.c_str(), duty / 100.0f);
            }
            return false;
        }
        case RuleType::THERM_DRT: {
            int mv = 0;
            if (PinRegistry::readMv(s0_name, mv) == ESP_OK) {
                float f;
                if (mv <= r.thresh_lo) {
                    f = 1.0f;
                } else if (mv >= r.thresh_hi) {
                    f = 0.0f;
                } else {
                    float range = (float)(r.thresh_hi - r.thresh_lo);
                    f = 1.0f - ((float)(mv - r.thresh_lo) / range);
                }
                PinRegistry::setDutyF(r.dst.c_str(), f);
            }
            return false;
        }
        case RuleType::PWM_OUT: {
            // Static PWM: set frequency param_a Hz, duty param_b %
            // This is handled at apply() time; nothing to compute per tick.
            return false;
        }

        default:
            return false;  // not yet implemented
    }
}

// ---------------------------------------------------------------------------
// evalTick: one evaluation cycle
// ---------------------------------------------------------------------------

static void evalTick(
    std::vector<RuleState>  &ruleStates,
    std::vector<VarState>   &varStates,
    IOConfigManager         &io,
    std::map<std::string, bool> &sig,
    TickType_t now)
{
    // 1. Read all physical pins -> sig
    for (const auto &p : io.pins()) {
        bool val = false;
        if (p.type == PinType::INPUT || p.type == PinType::OUTPUT) {
            if (PinRegistry::read(p.name.c_str(), val) == ESP_OK)
                sig[p.name] = val;
        }
        // ADC pins: leave as false in sig (read via readMv inside computeRule)
    }

    // 2. Evaluate variables -> add to sig
    for (auto &vs : varStates) {
        if (vs.ast)
            sig[vs.name] = vs.ast->eval(sig);
    }

    // 3. For each rule, compute claimed value and accumulate into claims map
    //    For rules with group dsts, expand the group.
    std::map<std::string, bool> claims;

    // Initialize claims to false for all rule destinations (expand groups)
    for (const auto &s : ruleStates) {
        const std::string &dst = s.rule.dst;
        if (dst.empty()) continue;
        const IOGroup *grp = io.findGroup(dst.c_str());
        if (grp) {
            for (const auto &m : grp->members)
                if (claims.find(m) == claims.end()) claims[m] = false;
        } else {
            if (claims.find(dst) == claims.end()) claims[dst] = false;
        }
    }

    for (auto &s : ruleStates) {
        // ADC/PWM side-effect rules return false; skip them for claims
        bool val = computeRule(s, sig, now);

        // Skip side-effect-only rules
        if (s.rule.type == RuleType::ADC_MAP ||
            s.rule.type == RuleType::THERM_DRT ||
            s.rule.type == RuleType::PWM_OUT)
            continue;

        const std::string &dst = s.rule.dst;
        if (dst.empty()) continue;

        // Check if dst is a group name
        const IOGroup *grp = io.findGroup(dst.c_str());
        if (grp) {
            for (const auto &m : grp->members)
                claims[m] = claims[m] || val;
        } else {
            claims[dst] = claims[dst] || val;
        }
    }

    // 4. Apply claims: update sig and drive physical pins (skip group names)
    for (const auto &kv : claims) {
        if (io.findGroup(kv.first.c_str())) continue;
        sig[kv.first] = kv.second;
        PinRegistry::set(kv.first.c_str(), kv.second);
    }
}

// ---------------------------------------------------------------------------
// MainController
// ---------------------------------------------------------------------------

MainController::MainController()
    : Task("main_ctrl", 8192, configMAX_PRIORITIES - 1, APP_CPU_NUM)
{}

MainController &MainController::instance()
{
    static MainController s_instance;
    return s_instance;
}

void MainController::start()
{
    ESP_LOGI(TAG, "starting on core %d, priority %d", APP_CPU_NUM, configMAX_PRIORITIES - 1);
    Task::start();
}

void MainController::requestReload()
{
    _reload.store(true);
}

void MainController::run()
{
    IOConfigManager &io = IOConfigManager::instance();

    while (true) {
        _reload.store(false);
        io.apply();

        if (io.pins().empty()) {
            ESP_LOGI(TAG, "no IO config, running demo");
            runDemo();
            continue;
        }

        // Build RuleState vector, parsing EXPR ASTs
        std::vector<RuleState> ruleStates;
        for (const auto &r : io.rules()) {
            RuleState rs;
            rs.rule = r;
            if (r.type == RuleType::EXPR && !r.expr.empty()) {
                std::string err;
                rs.ast = parseExpr(r.expr.c_str(), err);
                if (!rs.ast)
                    ESP_LOGW(TAG, "EXPR rule '%s': parse error: %s", r.dst.c_str(), err.c_str());
            }
            ruleStates.push_back(std::move(rs));
        }

        // Build VarState vector
        std::vector<VarState> varStates;
        for (const auto &v : io.vars()) {
            VarState vs;
            vs.name = v.name;
            std::string err;
            vs.ast = parseExpr(v.expr.c_str(), err);
            if (!vs.ast)
                ESP_LOGW(TAG, "var '%s': parse error: %s", v.name.c_str(), err.c_str());
            varStates.push_back(std::move(vs));
        }

        ESP_LOGI(TAG, "%d pin(s), %d rule(s), %d var(s), %d group(s)",
                 (int)io.pins().size(), (int)ruleStates.size(),
                 (int)varStates.size(), (int)io.groups().size());

        // Initialise oscillator timestamps
        TickType_t now = xTaskGetTickCount();
        for (auto &s : ruleStates)
            s.last_flash = now;

        std::map<std::string, bool> sig;

        while (!_reload.load()) {
            now = xTaskGetTickCount();
            evalTick(ruleStates, varStates, io, sig, now);
            vTaskDelay(1);
        }
        ESP_LOGI(TAG, "config reload triggered");
    }
}

void MainController::runDemo()
{
    PinRegistry::addOutput("LED1", GPIO_NUM_20);
    PinRegistry::addOutput("LED2", GPIO_NUM_21);
    PinRegistry::addInput ("SW1",  GPIO_NUM_22, GPIO_PULLUP_ENABLE);

    ESP_LOGI(TAG, "SW1 initial raw level: %d", gpio_get_level(GPIO_NUM_22));

    TickType_t lastLed1 = xTaskGetTickCount();
    TickType_t lastLed2 = lastLed1;

    while (!_reload.load()) {
        bool held = false;
        PinRegistry::read("SW1", held);

        if (held) {
            PinRegistry::set("LED1", true);
            PinRegistry::set("LED2", true);
            lastLed1 = xTaskGetTickCount();
            lastLed2 = lastLed1;
            vTaskDelay(1);
            continue;
        }

        TickType_t now = xTaskGetTickCount();
        if ((now - lastLed1) >= pdMS_TO_TICKS(500)) {
            PinRegistry::toggle("LED1");
            lastLed1 = now;
        }
        if ((now - lastLed2) >= pdMS_TO_TICKS(1000)) {
            PinRegistry::toggle("LED2");
            lastLed2 = now;
        }
        vTaskDelay(1);
    }
}
