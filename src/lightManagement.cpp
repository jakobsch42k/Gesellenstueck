#include "lightManagement.h"
#include "actuators.h"
#include "constants.h"

// Proportional gain: lux error → PWM change per update cycle
static const float Kp = 0.1f;

// Slew-rate limit: smooth visible fades instead of abrupt steps
#define LIGHT_RAMP_INTERVAL_MS  20   // 50 Hz output update
#define LIGHT_RAMP_STEP_MAX      1   // ≈5 s for full 0↔255 fade

static int targetPWM  = 0;
static int currentPWM = 0;
static unsigned long lastRamp = 0;

void lightManagement_init() {
    targetPWM  = 0;
    currentPWM = 0;
    led_grow_setPWM(0);
    Serial.println("[lightManagement] init OK");
}

void lightManagement_update(const LiveData& data, const Config& cfg, const ErrorFlags& err) {
    int hour = (int)(data.timeOfDay / 3600) % 24;
    int profilePercent = cfg.lightProfile[hour];

    // Profile == 0 means LED off for this hour
    if (profilePercent == 0) {
        targetPWM = 0;
    }
    // Sensor broken: open-loop fallback — scale profile% directly to PWM
    else if (err.ERR_SENSOR_BH) {
        targetPWM = constrain((int)(profilePercent / 100.0f * 255), 0, 255);
    }
    // Closed-loop P-controller using smoothed lux
    else {
        float targetLux = cfg.luxTarget * (profilePercent / 100.0f);
        float error     = targetLux - data.luxSmoothed;
        if (abs(error) >= LUX_HYSTERESIS) {
            targetPWM = constrain(currentPWM + (int)(error * Kp), 0, 255);
        }
    }

    // Slew currentPWM toward targetPWM for smooth visual transitions
    unsigned long now = millis();
    if (now - lastRamp >= LIGHT_RAMP_INTERVAL_MS) {
        lastRamp = now;
        if (currentPWM < targetPWM) {
            currentPWM = min(currentPWM + LIGHT_RAMP_STEP_MAX, targetPWM);
            led_grow_setPWM(currentPWM);
        } else if (currentPWM > targetPWM) {
            currentPWM = max(currentPWM - LIGHT_RAMP_STEP_MAX, targetPWM);
            led_grow_setPWM(currentPWM);
        }
    }
}

int lightManagement_getCurrentPWM() {
    return currentPWM;
}

void lightManagement_resetPWM() {
    currentPWM = -1; // force recalculation/ramp on next update
}
