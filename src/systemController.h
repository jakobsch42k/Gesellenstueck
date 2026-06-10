#pragma once

#include "shared.h"
#include "actuators.h"

class SystemController {
public:
    void init();
    void run();

private:
    LiveData   liveData   = {};
    Config     config     = {};
    ErrorFlags errorFlags = {};
    Actuators  actuators;
    bool           prevRoofClosed      = false;
    unsigned long  maintenanceOpenUntil = 0;

    bool handleManualCommand(String cmd, int val);
    void handleAckErrors();
};
