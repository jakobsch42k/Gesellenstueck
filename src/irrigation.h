#pragma once

#include "shared.h"
#include "actuators.h"

enum IrrigationState {
    IRRIGATION_IDLE,
    IRRIGATION_PUMPING,
    IRRIGATION_PAUSING
};

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

// Temporary shim for webBackend until the ControlStatus decoupling step (A2
// last step): forwards to the SystemController-owned instance.
IrrigationState irrigation_getState();
