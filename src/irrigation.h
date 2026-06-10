#pragma once

#include "shared.h"
#include "actuators.h"

// Bed-scanning irrigation FSM. Owned by SystemController; the instance lives
// for the full program duration, so the Actuators pointer stays valid.
class IrrigationController {
public:
    void            init(Actuators& actuators);
    void            update(const LiveData& data, const Config& cfg, ErrorFlags& err);
    IrrigationState getState() const { return state; }
    int             getActiveBed() const { return activeBed; } // -1 when idle

private:
    void enterState(IrrigationState next);

    Actuators*      act            = nullptr;
    IrrigationState state          = IRRIGATION_IDLE;
    unsigned long   stateEnteredAt = 0;
    int             activeBed      = -1;
};
