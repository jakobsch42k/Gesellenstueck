#include "irrigation.h"
#include "actuators.h"
#include "logger.h"

void IrrigationController::enterState(IrrigationState next) {
    state          = next;
    stateEnteredAt = millis();
}

void IrrigationController::init(Actuators& actuators) {
    act = &actuators;
    act->valve_closeAll();
    act->pump_off();
    enterState(IRRIGATION_IDLE);
    logger.info("irrigation", "init OK");
}

void IrrigationController::update(const LiveData& data, Config& cfg, ErrorFlags& err) {
    // Hard stop: critical water level or emergency stop locks irrigation
    if (err.ERR_WATER_CRITICAL || err.EMERGENCY_STOP) {
        if (state != IRRIGATION_IDLE) {
            act->pump_off();
            act->valve_closeAll();
            enterState(IRRIGATION_IDLE);
            activeBed = -1;
            logger.warn("irrigation", "locked — water critical or emergency stop");
        }
        return;
    }

    // Low water warning — log but continue
    if (!data.waterLow && state == IRRIGATION_IDLE) {
        err.lastErrorMessage = "Water level low — refill soon";
    }

    // Expire per-bed diffusion lockouts. Each bed soaks on its own clock; once
    // irrigationPause_ms elapses the bed is eligible for re-watering again.
    for (int i = 0; i < NUM_BEDS; i++) {
        if (diffusing[i] && (millis() - lockedAt[i]) >= cfg.irrigationPause_ms)
            diffusing[i] = false;
    }

    unsigned long elapsed = millis() - stateEnteredAt;

    switch (state) {

        case IRRIGATION_IDLE: {
            // Round-robin scan: start from cfg.nextBed and wrap, watering the
            // first dry bed found. Rotating the start point keeps beds fair under
            // contention so no bed starves behind a perpetually thirsty bed 1.
            // Beds still in their diffusion lockout are skipped so we never
            // re-pump a freshly watered bed before its soil reading settles.
            // Bounded to NUM_BEDS iterations — safe even if all beds are
            // faulty/moist/soaking (loop simply finds nothing and stays IDLE).
            for (int k = 0; k < NUM_BEDS; k++) {
                int i = (cfg.nextBed + k) % NUM_BEDS;
                if (err.ERR_SOIL[i])  continue; // skip faulty sensors
                if (diffusing[i])     continue; // skip beds still soaking
                if (cfg.bedLocked[i]) continue; // skip manually locked beds
                if (data.soilPerc[i] < cfg.moisture[i]) {
                    activeBed = i;
                    act->valve_open(activeBed);
                    act->pump_on(cfg.pumpPwm);
                    enterState(IRRIGATION_PUMPING);
                    logger.info("irrigation", "bed " + String(i + 1) +
                                " dry (" + String(data.soilPerc[i]) + "%) — pumping");
                    break;
                }
            }
            break;
        }

        case IRRIGATION_PUMPING:
            if (elapsed >= cfg.irrigationDuration_ms) {
                act->valve_close(activeBed);
                act->pump_off();
                // Start this bed's solo diffusion lockout, then return straight
                // to IDLE so the scanner can water the next dry bed immediately —
                // no global pause blocking the rest of the beds.
                diffusing[activeBed] = true;
                lockedAt[activeBed]  = millis();
                // Advance the rotation pointer past the bed just served so the
                // next bed gets first dibs (bed 5 → bed 1 wrap).
                cfg.nextBed = (activeBed + 1) % NUM_BEDS;
                logger.info("irrigation", "bed " + String(activeBed + 1) +
                            " — pump done, diffusing");
                activeBed = -1;
                enterState(IRRIGATION_IDLE);
            }
            break;
    }
}
