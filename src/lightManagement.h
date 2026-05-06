#pragma once

#include "shared.h"

void lightManagement_init();
void lightManagement_update(const LiveData& data, const Config& cfg, const ErrorFlags& err);
int  lightManagement_getCurrentPWM();
