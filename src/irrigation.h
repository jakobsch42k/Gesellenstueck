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
    // Milliseconds left in the active pump cycle (0 unless PUMPING). Read-only.
    unsigned long   getStateRemainingMs(const Config& cfg) const {
        if (state != IRRIGATION_PUMPING) return 0UL;
        unsigned long elapsed = millis() - stateEnteredAt;
        return (elapsed >= cfg.irrigationDuration_ms)
                   ? 0UL : (cfg.irrigationDuration_ms - elapsed);
    }
    // Diffusion (re-water lockout) ms left for one bed, 0 = ready. Each bed soaks
    // on its own clock so the scanner can water the next dry bed immediately.
    unsigned long   getBedDiffuseMs(int bed, const Config& cfg) const {
        if (bed < 0 || bed >= NUM_BEDS || !diffusing[bed]) return 0UL;
        unsigned long elapsed = millis() - lockedAt[bed];
        return (elapsed >= cfg.irrigationPause_ms)
                   ? 0UL : (cfg.irrigationPause_ms - elapsed);
    }

private:
    void enterState(IrrigationState next);

    Actuators*      act            = nullptr;
    IrrigationState state          = IRRIGATION_IDLE;
    unsigned long   stateEnteredAt = 0;
    int             activeBed      = -1;
    // Per-bed diffusion lockout. diffusing[i] gates re-watering until
    // irrigationPause_ms elapses from lockedAt[i]. false at boot = all ready.
    bool            diffusing[NUM_BEDS] = {false};
    unsigned long   lockedAt[NUM_BEDS]  = {0};
};
