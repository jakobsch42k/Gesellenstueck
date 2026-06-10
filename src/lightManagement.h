#pragma once

#include "shared.h"
#include "actuators.h"

void lightManagement_init(Actuators& act);
void lightManagement_update(const LiveData& data, const Config& cfg, const ErrorFlags& err);
int  lightManagement_getCurrentPWM();
void lightManagement_resetPWM();
