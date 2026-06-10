#pragma once

#include "shared.h"
#include "actuators.h"

enum IrrigationState {
    IRRIGATION_IDLE,
    IRRIGATION_PUMPING,
    IRRIGATION_PAUSING
};

void            irrigation_init(Actuators& act);
void            irrigation_update(const LiveData& data, const Config& cfg, ErrorFlags& err);
IrrigationState irrigation_getState();
int             irrigation_getActiveBed(); // -1 when idle
