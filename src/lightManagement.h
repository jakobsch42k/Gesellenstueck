#pragma once

#include "shared.h"
#include "actuators.h"

// Feedforward + PI grow-light controller. Owned by SystemController; the
// instance lives for the full program duration, so the Actuators pointer
// stays valid.
class LightController {
public:
    void init(Actuators& actuators);
    void update(const LiveData& data, const Config& cfg, const ErrorFlags& err);
    int  getCurrentPWM() const { return currentPWM; }
    void resetPWM();

private:
    Actuators*    act         = nullptr;
    int           targetPWM   = 0;
    int           currentPWM  = 0;
    float         integral    = 0.0f;
    unsigned long lastRamp    = 0;
    unsigned long lastControl = 0;
};
