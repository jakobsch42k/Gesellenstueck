#pragma once

#include "shared.h"
#include "actuators.h"

enum RoofState {
    ROOF_IDLE,
    ROOF_OPENING,
    ROOF_OPEN,
    ROOF_CLOSING,
    ROOF_CLOSED,
    ROOF_ERROR
};

// Temperature-driven roof FSM. Owned by SystemController; the instance lives
// for the full program duration, so the Actuators pointer stays valid.
class RoofController {
public:
    void      init(const LiveData& data, ErrorFlags& err, Actuators& actuators);
    void      update(const LiveData& data, const Config& cfg, ErrorFlags& err);
    RoofState getState() const { return state; }
    void      setState(RoofState s);   // used by manual commands in maintenance mode
    void      ackError(ErrorFlags& err);

private:
    void enterState(RoofState next);

    Actuators*    act             = nullptr;
    RoofState     state           = ROOF_IDLE;
    unsigned long stateEnteredAt  = 0;
    bool          bmeStaleLatched = false;
    bool          reedWarnedThisStroke = false;
};

// Temporary shim for webBackend until the ControlStatus decoupling step (A2
// last step): forwards to the SystemController-owned instance.
RoofState roofControl_getState();
