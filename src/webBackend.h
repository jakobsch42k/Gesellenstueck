#pragma once

#include "shared.h"
#include <functional>

// Note: Config is non-const — /saveConfig and /importConfig update it in place.
// Lifetime contract: all referenced objects are owned by SystemController and
// live for the full program duration; webBackend keeps raw pointers to them.
void webBackend_init(const LiveData& data, Config& cfg, ErrorFlags& err,
                     const ControlStatus& status);

void webBackend_registerCallbacks(
    std::function<bool(String, int)>   manualCtrlCb,  // returns false = command refused (HTTP 409)
    std::function<void()>              ackErrorsCb,
    std::function<void(unsigned long)> setTimeCb
);

void webBackend_handle();
