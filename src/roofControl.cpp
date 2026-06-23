#include "roofControl.h"
#include "actuators.h"
#include "constants.h"
#include "logger.h"

void RoofController::enterState(RoofState next) {
    state          = next;
    stateEnteredAt = millis();
    reedWarnedThisStroke = false;
}

void RoofController::init(const LiveData& data, ErrorFlags& err, Actuators& actuators) {
    act = &actuators;
    // Set initial state from reed contact so motor doesn't run on reboot
    if (data.roofClosed) {
        enterState(ROOF_CLOSED);
        logger.info("roofControl", "init — roof CLOSED");
    } else {
        enterState(ROOF_OPEN);
        logger.info("roofControl", "init — roof OPEN");
    }
}

void RoofController::update(const LiveData& data, const Config& cfg, ErrorFlags& err) {
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
        logger.warn("roofControl", bmeStale
            ? "BME280 data stale — safe-opening roof, regulation suspended"
            : "BME280 data fresh — regulation resumed");
    }

    switch (state) {

        case ROOF_IDLE:
        case ROOF_CLOSED:
            // Safe-open on stale data
            if (bmeStale) {
                act->roof_open(cfg.roofOpenPwm);
                enterState(ROOF_OPENING);
                logger.info("roofControl", "stale sensor data — opening roof");
                break;
            }
            // Min dwell: suppress temperature-driven strokes until the state
            // has settled, so threshold noise can't chatter the motor. Stale
            // safe-open (above) and manual commands are never delayed.
            if (elapsed < ROOF_MIN_DWELL_MS) break;
            // Open if too warm
            if (data.tempC > cfg.tempTarget + cfg.tempHysteresis) {
                act->roof_open(cfg.roofOpenPwm);
                enterState(ROOF_OPENING);
                logger.info("roofControl", "too warm — opening roof");
            }
            break;

        case ROOF_OPENING:
            // Reed sanity check: contact should release shortly after the
            // motor starts. If it still reads closed past the grace period,
            // something is mechanically wrong (or the reed is sticky). Policy:
            // log once and continue the stroke — aborting would brick the
            // roof on a flaky contact; the warning makes the fault visible.
            if (data.roofClosed && elapsed > ROOF_REED_GRACE_MS && !reedWarnedThisStroke) {
                reedWarnedThisStroke = true;
                logger.warn("roofControl", "reed still closed while opening — check mechanism");
            }
            // Opening is time-based (no open-position end stop)
            if (elapsed >= cfg.roofOpenDuration_ms) {
                act->roof_stop();
                enterState(ROOF_OPEN);
                logger.info("roofControl", "roof open");
            }
            break;

        case ROOF_OPEN:
            // Hold open on stale data — never close on a frozen reading
            if (bmeStale) break;
            // Min dwell — see ROOF_CLOSED case
            if (elapsed < ROOF_MIN_DWELL_MS) break;
            // Close if cool enough
            if (data.tempC < cfg.tempTarget - cfg.tempHysteresis) {
                act->roof_close(cfg.roofClosePwm);
                enterState(ROOF_CLOSING);
                logger.info("roofControl", "cool enough — closing roof");
            }
            break;

        case ROOF_CLOSING:
            // Reed contact confirms end position
            if (data.roofClosed) {
                act->roof_stop();
                enterState(ROOF_CLOSED);
                logger.info("roofControl", "roof closed — reed contact OK");
                break;
            }
            // Timeout: reed never triggered
            if (elapsed >= cfg.roofCloseTimeout_ms) {
                act->roof_stop();
                err.ERR_ROOF_TIMEOUT    = true;
                err.lastErrorMessage    = "Roof close timeout — reed not reached";
                enterState(ROOF_ERROR);
                logger.error("roofControl", "close timeout — reed not reached");
            }
            break;

        default:
            break;
    }
}

void RoofController::setState(RoofState s) {
    enterState(s);
}

void RoofController::syncFromHardware(const LiveData& data) {
    enterState(data.roofClosed ? ROOF_CLOSED : ROOF_OPEN);
    // Pre-age the dwell timer so auto regulation can respond immediately
    // after switching out of maintenance mode.
    stateEnteredAt -= ROOF_MIN_DWELL_MS;
}

void RoofController::ackError(ErrorFlags& err) {
    if (state == ROOF_ERROR) {
        err.ERR_ROOF_TIMEOUT = false;
        enterState(ROOF_IDLE);
        logger.info("roofControl", "error acknowledged — returning to IDLE");
    }
}
