#include "irrigation.h"
#include "actuators.h"

static IrrigationState state          = IRRIGATION_IDLE;
static unsigned long   stateEnteredAt = 0;
static int             activeBed      = -1;

static void enterState(IrrigationState next) {
    state          = next;
    stateEnteredAt = millis();
}

void irrigation_init() {
    valve_closeAll();
    pump_off();
    enterState(IRRIGATION_IDLE);
    Serial.println("[irrigation] init OK");
}

void irrigation_update(const LiveData& data, const Config& cfg, ErrorFlags& err) {
    // Hard stop: critical water level or emergency stop locks irrigation
    if (err.ERR_WATER_CRITICAL || err.EMERGENCY_STOP) {
        if (state != IRRIGATION_IDLE) {
            pump_off();
            valve_closeAll();
            enterState(IRRIGATION_IDLE);
            activeBed = -1;
            Serial.println("[irrigation] locked — water critical or emergency stop");
        }
        return;
    }

    // Low water warning — log but continue
    if (!data.waterLow && state == IRRIGATION_IDLE) {
        err.lastErrorMessage = "Water level low — refill soon";
    }

    unsigned long elapsed = millis() - stateEnteredAt;

    switch (state) {

        case IRRIGATION_IDLE: {
            // Scan beds in order, start with first dry one
            for (int i = 0; i < NUM_BEDS; i++) {
                if (err.ERR_SOIL[i]) continue; // skip faulty sensors
                if (data.soilPerc[i] < cfg.moisture[i]) {
                    activeBed = i;
                    valve_open(activeBed);
                    pump_on();
                    enterState(IRRIGATION_PUMPING);
                    Serial.println("[irrigation] bed " + String(i + 1) +
                                   " dry (" + String(data.soilPerc[i]) +
                                   "%) — pumping");
                    break;
                }
            }
            break;
        }

        case IRRIGATION_PUMPING:
            if (elapsed >= cfg.irrigationDuration_ms) {
                valve_close(activeBed);
                pump_off();
                enterState(IRRIGATION_PAUSING);
                Serial.println("[irrigation] bed " + String(activeBed + 1) +
                               " — pump done, diffusion pause");
            }
            break;

        case IRRIGATION_PAUSING:
            if (elapsed >= cfg.irrigationPause_ms) {
                activeBed = -1;
                enterState(IRRIGATION_IDLE);
                // IDLE will re-check moisture on next call
                Serial.println("[irrigation] pause done — re-checking moisture");
            }
            break;
    }
}

IrrigationState irrigation_getState() {
    return state;
}

int irrigation_getActiveBed() {
    return activeBed;
}
