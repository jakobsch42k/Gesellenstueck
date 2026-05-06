#pragma once

#include "shared.h"

class SystemController {
public:
    void init();
    void run();

private:
    LiveData   liveData   = {};
    Config     config     = {};
    ErrorFlags errorFlags = {};

    void handleManualCommand(String cmd, int val);
    void handleAckErrors();
};
