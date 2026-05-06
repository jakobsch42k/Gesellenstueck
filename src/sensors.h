#pragma once

#include "shared.h"

void sensors_init(LiveData& data, ErrorFlags& err, const Config& cfg);
void sensors_update(LiveData& data, ErrorFlags& err, const Config& cfg);
void sensors_setTime(LiveData& data, unsigned long unixTimestamp);
