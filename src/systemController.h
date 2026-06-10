#pragma once

#include "shared.h"
#include "actuators.h"
#include "sensors.h"
#include "roofControl.h"
#include "irrigation.h"
#include "lightManagement.h"

class SystemController {
public:
    void init();
    void run();

private:
    LiveData   liveData   = {};
    Config     config     = {};
    ErrorFlags errorFlags = {};
    Actuators      actuators;
    Sensors        sensors;
    RoofController       roofControl;
    IrrigationController irrigation;
    LightController      lightControl;
    bool           prevRoofClosed      = false;
    unsigned long  maintenanceOpenUntil = 0;

    bool handleManualCommand(String cmd, int val);
    void handleAckErrors();
};
