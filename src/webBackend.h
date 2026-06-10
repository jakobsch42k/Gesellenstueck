#pragma once

#include "shared.h"
#include <functional>

// Note: Config is non-const — /saveConfig and /importConfig update it in place
void webBackend_init(const LiveData& data, Config& cfg, ErrorFlags& err);

void webBackend_registerCallbacks(
    std::function<bool(String, int)>   manualCtrlCb,  // returns false = command refused (HTTP 409)
    std::function<void()>              ackErrorsCb,
    std::function<void(unsigned long)> setTimeCb
);

void webBackend_handle();
