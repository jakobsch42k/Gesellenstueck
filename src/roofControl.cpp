#include "roofControl.h"
#include "actuators.h"
#include "constants.h"

static RoofState     state           = ROOF_IDLE;
static unsigned long stateEnteredAt  = 0;
static bool          bmeStaleLatched = false;

static void enterState(RoofState next) {
    state          = next;
    stateEnteredAt = millis();
}

void roofControl_init(const LiveData& data, ErrorFlags& err) {
    // Set initial state from reed contact so motor doesn't run on reboot
    if (data.roofClosed) {
        enterState(ROOF_CLOSED);
        Serial.println("[roofControl] init — roof CLOSED");
    } else {
        enterState(ROOF_OPEN);
        Serial.println("[roofControl] init — roof OPEN");
    }
}

void roofControl_update(const LiveData& data, const Config& cfg, ErrorFlags& err) {
    // Error state: motor stopped, no transitions until acknowledged
    if (state == ROOF_ERROR) return;

    unsigned long elapsed = millis() - stateEnteredAt;

    // Stale-data guard: BME280 frozen → tempC can't be trusted. Policy is
    // safe-open: an open roof can't overheat the bed; rain exposure is the
    // lesser risk. While stale, never start a closing stroke; an in-progress
    // stroke may finish (reed contact / timeout still supervise it). Normal
    // regulation resumes automatically on the first fresh read.
    bool bmeStale = (millis() - data.lastBmeReadMs) > SENSOR_STALE_TIMEOUT_MS;
    if (bmeStale != bmeStaleLatched) {
        bmeStaleLatched = bmeStale;
        Serial.println(bmeStale
            ? "[roofControl] BME280 data stale — safe-opening roof, regulation suspended"
            : "[roofControl] BME280 data fresh — regulation resumed");
    }

    switch (state) {

        case ROOF_IDLE:
        case ROOF_CLOSED:
            // Safe-open on stale data
            if (bmeStale) {
                roof_open();
                enterState(ROOF_OPENING);
                Serial.println("[roofControl] stale sensor data — opening roof");
                break;
            }
            // Open if too warm
            if (data.tempC > cfg.tempTarget + cfg.tempHysteresis) {
                roof_open();
                enterState(ROOF_OPENING);
                Serial.println("[roofControl] too warm — opening roof");
            }
            break;

        case ROOF_OPENING:
            // Opening is time-based (no open-position end stop)
            if (elapsed >= cfg.roofOpenDuration_ms) {
                roof_stop();
                enterState(ROOF_OPEN);
                Serial.println("[roofControl] roof open");
            }
            break;

        case ROOF_OPEN:
            // Hold open on stale data — never close on a frozen reading
            if (bmeStale) break;
            // Close if cool enough
            if (data.tempC < cfg.tempTarget - cfg.tempHysteresis) {
                roof_close();
                enterState(ROOF_CLOSING);
                Serial.println("[roofControl] cool enough — closing roof");
            }
            break;

        case ROOF_CLOSING:
            // Reed contact confirms end position
            if (data.roofClosed) {
                roof_stop();
                enterState(ROOF_CLOSED);
                Serial.println("[roofControl] roof closed — reed contact OK");
                break;
            }
            // Timeout: reed never triggered
            if (elapsed >= cfg.roofCloseTimeout_ms) {
                roof_stop();
                err.ERR_ROOF_TIMEOUT    = true;
                err.lastErrorMessage    = "Roof close timeout — reed not reached";
                enterState(ROOF_ERROR);
                Serial.println("[roofControl] ERROR: close timeout");
            }
            break;

        default:
            break;
    }
}

RoofState roofControl_getState() {
    return state;
}

void roofControl_setState(RoofState s) {
    enterState(s);
}

void roofControl_ackError(ErrorFlags& err) {
    if (state == ROOF_ERROR) {
        err.ERR_ROOF_TIMEOUT = false;
        enterState(ROOF_IDLE);
        Serial.println("[roofControl] error acknowledged — returning to IDLE");
    }
}
