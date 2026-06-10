#include "lightManagement.h"
#include "actuators.h"
#include "constants.h"

#define CONTROL_INTERVAL_MS      LUX_SAMPLE_INTERVAL_MS   // controller runs at sensor rate

void LightController::init(Actuators& actuators) {
    act = &actuators;
    targetPWM   = 0;
    currentPWM  = 0;
    integral    = 0.0f;
    act->led_grow_setPWM(0);
    Serial.println("[lightManagement] init OK");
}

void LightController::update(const LiveData& data, const Config& cfg, const ErrorFlags& err) {
    int hour = (int)(data.timeOfDay / 3600) % 24;
    int profilePercent = constrain(cfg.lightProfile[hour], 0, 100);
    int feedforward    = (profilePercent * 255) / 100;

    if (profilePercent == 0) {
        targetPWM = 0;
        integral  = 0.0f;
    }
    // Open-loop: sensor missing → feedforward only
    else if (err.ERR_SENSOR_BH) {
        targetPWM = feedforward;
    }
    // Feedforward + integrator with anti-windup, run at sensor sample rate
    else if (millis() - lastControl >= CONTROL_INTERVAL_MS) {
        lastControl = millis();

        float targetLux = cfg.luxTarget * (profilePercent / 100.0f);
        float error     = targetLux - data.luxSmoothed;

        // Anti-windup: don't integrate further into a saturated output, and stop
        // accumulating once inside the hysteresis band so we settle cleanly.
        bool atMax = (targetPWM >= 255) && (error > 0);
        bool atMin = (targetPWM <= 0)   && (error < 0);
        if (!atMax && !atMin && fabsf(error) >= LUX_HYSTERESIS) {
            integral += LIGHT_KI * error;
        }
        // Clamp integral so feedforward + integral never demands outside [0, 255]
        integral = constrain(integral, (float)(-feedforward), (float)(255 - feedforward));

        targetPWM = constrain(feedforward + (int)integral, 0, 255);
    }

    // Slew currentPWM toward targetPWM for smooth visual transitions
    unsigned long now = millis();
    if (now - lastRamp >= LIGHT_RAMP_INTERVAL_MS) {
        lastRamp = now;
        if (currentPWM < targetPWM) {
            currentPWM = min(currentPWM + LIGHT_RAMP_STEP_MAX, targetPWM);
            act->led_grow_setPWM(currentPWM);
        } else if (currentPWM > targetPWM) {
            currentPWM = max(currentPWM - LIGHT_RAMP_STEP_MAX, targetPWM);
            act->led_grow_setPWM(currentPWM);
        }
    }
}

void LightController::resetPWM() {
    currentPWM = -1; // force recalculation/ramp on next update
    integral   = 0.0f;
}
