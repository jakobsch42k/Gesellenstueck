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
    bool           prevRoofClosed      = false;
    unsigned long  maintenanceOpenUntil = 0;

    void handleManualCommand(String cmd, int val);
    void handleAckErrors();
};
