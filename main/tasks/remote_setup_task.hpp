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
    void onStartSetup();
    void onSaveSetup();
    void onCancelSetup();
    void sendResponse(const char *msg);

    bool          _remote_setup_mode = false;
    char          _line_buf[128]     = {};
    int           _line_len          = 0;
    QueueHandle_t _rx_queue          = nullptr;
};
