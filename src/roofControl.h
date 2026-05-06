#pragma once

#include "shared.h"

enum RoofState {
    ROOF_IDLE,
    ROOF_OPENING,
    ROOF_OPEN,
    ROOF_CLOSING,
    ROOF_CLOSED,
    ROOF_ERROR
};

void      roofControl_init(const LiveData& data, ErrorFlags& err);
void      roofControl_update(const LiveData& data, const Config& cfg, ErrorFlags& err);
RoofState roofControl_getState();
void      roofControl_ackError(ErrorFlags& err);
