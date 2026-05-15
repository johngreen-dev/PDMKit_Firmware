#pragma once

#include "task.hpp"
#include "tinyusb_cdc_acm.h"
#include "freertos/queue.h"

class RemoteSetupTask : public Task {
public:
    RemoteSetupTask();

protected:
    void run() override;

private:
    static void onRxCallback(int itf, cdcacm_event_t *event);

    void handleCommand(const char *cmd);

    // session commands
    void onStartSetup();
    void onSaveSetup();
    void onCancelSetup();

    // diagnostic
    void onGetStorage();

    // I/O config commands (require setup mode)
    void onAddOutput(const char *args);  // <name> <gpio>
    void onAddInput (const char *args);  // <name> <gpio> [up|down|none]
    void onAddADC   (const char *args);  // <name> <unit> <channel>
    void onAddPWM   (const char *args);  // <name> <gpio> [freq_hz]
    void onRemovePin(const char *args);  // <name>

    // Rule config commands (require setup mode)
    void onAddRule   (const char *args);  // <type> <src> <dst>  or  flash <dst> <on> <off>
    void onRemoveRule(const char *args);  // <index>
    void onListRules ();

    // Variable config (require setup mode)
    void onAddVar   (const char *args);  // <name> <expr...>
    void onRemoveVar(const char *args);  // <name>
    void onListVars ();

    // Group config (require setup mode)
    void onAddGroup   (const char *args);  // <name> <m1> [m2...]
    void onRemoveGroup(const char *args);  // <name>
    void onListGroups ();

    // Board pin map query (works outside setup mode)
    void onListBoardPins();

    // I/O query / control (work outside setup mode too)
    void onListPins ();
    void onSetOutput(const char *args);  // <name> <0|1>
    void onGetInput (const char *args);  // <name>

    // CAN runtime config (work outside setup mode)
    void onSetCANBaud(const char *args); // <kbps>

    void sendResponse(const char *msg);

    bool          _setup_mode      = false;
    char          _line_buf[512]   = {};
    int           _line_len        = 0;
    QueueHandle_t _rx_queue        = nullptr;
};
