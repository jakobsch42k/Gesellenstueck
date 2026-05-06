#include "lightManagement.h"
#include "actuators.h"
#include "constants.h"

// Proportional gain: lux error → PWM change per update cycle
static const float Kp = 0.1f;

static int currentPWM = 0;

void lightManagement_init() {
    currentPWM = 0;
    led_grow_setPWM(0);
    Serial.println("[lightManagement] init OK");
}

void lightManagement_update(const LiveData& data, const Config& cfg, const ErrorFlags& err) {
    int hour = (int)(data.timeOfDay / 3600) % 24;
    int profilePercent = cfg.lightProfile[hour];

    // Profile == 0 means LED off for this hour
    if (profilePercent == 0) {
        if (currentPWM != 0) {
            currentPWM = 0;
            led_grow_setPWM(0);
        }
        return;
    }

    float targetLux = cfg.luxTarget * (profilePercent / 100.0f);

    // Sensor broken: open-loop fallback — scale profile% directly to PWM
    if (err.ERR_SENSOR_BH) {
        int openLoopPWM = (int)(profilePercent / 100.0f * 255);
        openLoopPWM = constrain(openLoopPWM, 0, 255);
        if (currentPWM != openLoopPWM) {
            currentPWM = openLoopPWM;
            led_grow_setPWM(currentPWM);
        }
        return;
    }

    // Closed-loop P-controller using smoothed lux
    float error = targetLux - data.luxSmoothed;

    if (abs(error) < LUX_HYSTERESIS) return; // within dead-band, no change

    int newPWM = currentPWM + (int)(error * Kp);
    newPWM = constrain(newPWM, 0, 255);

    if (newPWM != currentPWM) {
        currentPWM = newPWM;
        led_grow_setPWM(currentPWM);
    }
}

int lightManagement_getCurrentPWM() {
    return currentPWM;
}

void lightManagement_resetPWM() {
    currentPWM = -1; // force recalculation on next update
}
