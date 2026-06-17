#pragma once

#include "shared.h"
#include "actuators.h"

// Bed-scanning irrigation FSM. Owned by SystemController; the instance lives
// for the full program duration, so the Actuators pointer stays valid.
class IrrigationController {
public:
    void            init(Actuators& actuators);
    void            update(const LiveData& data, Config& cfg, ErrorFlags& err);
    IrrigationState getState() const { return state; }
    int             getActiveBed() const { return activeBed; } // -1 when idle
    // Milliseconds left in the current timed state (0 in IDLE). Used by the
    // web UI irrigation map for the diffusion/pump countdown. Read-only.
    unsigned long   getStateRemainingMs(const Config& cfg) const {
        unsigned long elapsed = millis() - stateEnteredAt;
        unsigned long total =
            (state == IRRIGATION_PUMPING) ? cfg.irrigationDuration_ms :
            (state == IRRIGATION_PAUSING) ? cfg.irrigationPause_ms    : 0UL;
        return (elapsed >= total) ? 0UL : (total - elapsed);
    }

private:
    void enterState(IrrigationState next);

    Actuators*      act            = nullptr;
    IrrigationState state          = IRRIGATION_IDLE;
    unsigned long   stateEnteredAt = 0;
    int             activeBed      = -1;
};
